// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/** Family key of a card for draw exclusion. v4 (§2-3-2): exclusion is per (family, rolled rarity) PAIR — same
	 *  family at a different rarity co-presents; see DrawCards. The key itself: the authored/derived CardFamily only
	 *  (the v1 AppliedEffect-GE-class fallback was removed — with polymorphic multi-effect cards there is no
	 *  single card-level GE to key on, and IsDataValid requires multi-effect cards to set CardFamily). */
	FName GetCardFamilyKey(const UFPSRCardDataAsset* Card)
	{
		return Card ? Card->CardFamily : NAME_None;
	}

	/** True if any of the card's effects targets a weapon (WeaponStat / WeaponBehavior). Mirrors v1's "weapon-scope
	 *  cards join the level-up pool only once a weapon is owned" gate — effect-based so routing is unchanged. */
	bool CardRequiresWeapon(const UFPSRCardDataAsset* Card)
	{
		return Card && Card->Effects.ContainsByPredicate(
			[](const TObjectPtr<UFPSRCardEffect>& E) { return E && E->RequiresWeapon(); });
	}

	// Returns the behavior fragment a card grants (first UCardEffect_WeaponBehavior's Fragment), or null.
	UFPSRWeaponFragment* GetCardBehaviorFragment(const UFPSRCardDataAsset* Card)
	{
		if (!Card)
		{
			return nullptr;
		}
		for (const TObjectPtr<UFPSRCardEffect>& E : Card->Effects)
		{
			if (const UCardEffect_WeaponBehavior* Beh = Cast<UCardEffect_WeaponBehavior>(E))
			{
				return Beh->Fragment;
			}
		}
		return nullptr;
	}
}

float UFPSRCardSubsystem::ComputeSynergyMultiplier(const TArray<FName>& CardTags, const TMap<FName, int32>& TagCounts,
	float BonusPerCard, int32 MaxStacks)
{
	// 카드가 태그를 여러 개 달았으면 가장 많이 투자한 태그 하나만 본다(합산 아님, §6 헤더 주석) — 합산하면 저작이
	// "태그를 많이 달수록 유리"로 왜곡된다.
	int32 BestCount = 0;
	for (const FName& Tag : CardTags)
	{
		if (const int32* Count = TagCounts.Find(Tag))
		{
			BestCount = FMath::Max(BestCount, *Count);
		}
	}
	return 1.0f + BonusPerCard * static_cast<float>(FMath::Min(BestCount, MaxStacks));
}

void UFPSRCardSubsystem::WeightedSampleWithoutReplacement(
	TArray<FFPSRCardDraw>& InOutCandidates, TArray<float>& InOutWeights,
	TArray<float>& InOutBaselineWeights, TArray<int32>& InOutGroupIds, int32 Count,
	TFunctionRef<bool(const FFPSRCardDraw& Selected, const FFPSRCardDraw& Candidate)> ExclusionPredicate,
	TArray<FFPSRCardDraw>& OutPicked)
{
	OutPicked.Reset();
	// Bound the reservation by the candidate pool, not by the raw Count (현행 계약 승계, §6). The Max(0) is not
	// cosmetic: TArray::Reserve routes a negative size to OnInvalidArrayNum (Array.h), so "FPSR.DrawCards -1"
	// would take down the process rather than just drawing nothing.
	OutPicked.Reserve(FMath::Min(FMath::Max(Count, 0), InOutCandidates.Num()));

	// GroupIds 가 비어 있으면 종래의 단순 가중 추출(레벨업 풀 경로, 거동 불변, §6). 비어 있지 않으면 매 추출을
	// 그룹 비중 보존 2단으로 나눈다(사용자 결정 C) — 아래 else 분기.
	const bool bGrouped = InOutGroupIds.Num() > 0;

	for (int32 i = 0; i < Count && InOutCandidates.Num() > 0; ++i)
	{
		int32 SelectedIndex = INDEX_NONE;

		if (!bGrouped)
		{
			float TotalWeight = 0.0f;
			for (const float W : InOutWeights)
			{
				TotalWeight += W;
			}
			if (TotalWeight <= 0.0f)
			{
				break;
			}

			const float Pick = FMath::FRandRange(0.0f, TotalWeight);
			float Cumulative = 0.0f;
			SelectedIndex = 0;
			for (int32 j = 0; j < InOutWeights.Num(); ++j)
			{
				Cumulative += InOutWeights[j];
				if (Pick <= Cumulative)
				{
					SelectedIndex = j;
					break;
				}
			}
		}
		else
		{
			// 1단계 — 남은 그룹을 BaselineWeights 합에 비례해 고른다(= 시너지가 없었다면 가졌을 비중). 이러면 한
			// 그룹의 몫이 다른 그룹이 쌓은 시너지 때문에 줄어들지 않는다("그룹 비중 보존").
			float TotalBaseline = 0.0f;
			for (const float W : InOutBaselineWeights)
			{
				TotalBaseline += W;
			}
			if (TotalBaseline <= 0.0f)
			{
				break;
			}

			const float GroupPick = FMath::FRandRange(0.0f, TotalBaseline);
			float GroupCumulative = 0.0f;
			int32 ChosenGroupId = InOutGroupIds[0];
			for (int32 j = 0; j < InOutBaselineWeights.Num(); ++j)
			{
				GroupCumulative += InOutBaselineWeights[j];
				if (GroupPick <= GroupCumulative)
				{
					ChosenGroupId = InOutGroupIds[j];
					break;
				}
			}

			// 2단계 — 그 그룹 안에서 InOutWeights(시너지 포함)에 비례해 고른다. 시너지는 여기, 그룹 안에서만
			// 재분배된다.
			float TotalGroupWeight = 0.0f;
			for (int32 j = 0; j < InOutGroupIds.Num(); ++j)
			{
				if (InOutGroupIds[j] == ChosenGroupId)
				{
					TotalGroupWeight += InOutWeights[j];
				}
			}

			const float Pick = FMath::FRandRange(0.0f, TotalGroupWeight);
			float Cumulative = 0.0f;
			for (int32 j = 0; j < InOutGroupIds.Num(); ++j)
			{
				if (InOutGroupIds[j] != ChosenGroupId)
				{
					continue;
				}
				if (SelectedIndex == INDEX_NONE)
				{
					SelectedIndex = j; // defensive fallback: first member of the group if TotalGroupWeight is 0
				}
				Cumulative += InOutWeights[j];
				if (Pick <= Cumulative)
				{
					SelectedIndex = j;
					break;
				}
			}
		}

		if (!InOutCandidates.IsValidIndex(SelectedIndex))
		{
			break;
		}

		const FFPSRCardDraw Selected = InOutCandidates[SelectedIndex];
		OutPicked.Add(Selected);

		for (int32 k = InOutCandidates.Num() - 1; k >= 0; --k)
		{
			// Self always goes, regardless of the predicate (헬퍼 계약 ①, §6) — guarantees the exact picked
			// candidate can't repeat even when the predicate never matches it (e.g. an unset family key).
			const bool bIsSelf = (k == SelectedIndex);
			const bool bExcluded = !bIsSelf && ExclusionPredicate(Selected, InOutCandidates[k]);
			if (bIsSelf || bExcluded)
			{
				InOutCandidates.RemoveAt(k);
				InOutWeights.RemoveAt(k);
				if (bGrouped)
				{
					InOutBaselineWeights.RemoveAt(k);
					InOutGroupIds.RemoveAt(k);
				}
			}
		}
	}
}

bool UFPSRCardSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

TArray<FFPSRCardDraw> UFPSRCardSubsystem::DrawCards(AController* ForPlayer, int32 Count, const TArray<UFPSRCardDataAsset*>& Exclude)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return {};
	}

	if (!ActivePool)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Card] DrawCards called but ActivePool is null"));
		return {};
	}

	TArray<UFPSRCardDataAsset*> Cards;
	TArray<UFPSRWeaponDataAsset*> SourceWeapons; // index-aligned with Cards: the weapon that contributed each
	GatherCandidatePool(ForPlayer, Cards, SourceWeapons);

	// Player luck shifts the draw toward higher rarities.
	float Luck = 0.0f;
	if (AFPSRPlayerState* PS = ForPlayer ? ForPlayer->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (UFPSRCombatSet* CombatSet = PS->GetCombatSet())
		{
			Luck = CombatSet->GetLuck();
		}
	}

	// Weapon-scope (ThisWeapon/AllWeapons) stat cards join the level-up pool only once the player owns a
	// weapon to apply them to (Game.MD §2-4-1). Character-scope cards are always eligible.
	UFPSRWeaponInventoryComponent* Inv = nullptr;
	if (APawn* Pawn = ForPlayer ? ForPlayer->GetPawn() : nullptr)
	{
		Inv = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	}
	const bool bHasWeapon = Inv && Inv->HasAnyOwnedWeapon();

	// Flatten each eligible card into one weighted offer per OFFERED rarity. Excluded cards, behavior-fragment
	// cards (U6/H2 routes them to UnlockableFeatures — mission/milestone only, never this level-up draw), and
	// weapon-targeting cards while the player owns no weapon are skipped.
	TArray<FFPSRCardDraw> Candidates;
	TArray<float> CandidateWeights;
	for (int32 CardIdx = 0; CardIdx < Cards.Num(); ++CardIdx)
	{
		UFPSRCardDataAsset* Card = Cards[CardIdx];
		UFPSRWeaponDataAsset* SourceWeapon = SourceWeapons.IsValidIndex(CardIdx) ? SourceWeapons[CardIdx] : nullptr;
		if (!Card || Exclude.Contains(Card))
		{
			continue;
		}
		if (Card->Effects.Num() == 0)
		{
			// Not silent: a misconfigured card with no effects is logged rather than vanishing from the draw.
			UE_LOG(LogFPSR, Warning, TEXT("[Card] '%s' has no Effects — skipped (configure at least one effect)."), *Card->GetName());
			continue;
		}
		// Weapon-targeting cards (this-weapon / all-weapons stat) join the pool only once a weapon is owned (v1 gate).
		if (CardRequiresWeapon(Card) && !bHasWeapon)
		{
			continue;
		}
		if (Card->OfferRarities.Num() == 0)
		{
			// 티어가 없는 카드는 레벨업 오퍼를 만들 수 없다. 행동 프래그먼트 카드(티어 없음)는 미션/마일스톤 풀
			// (Weapon.UnlockableFeatures)에 속한다(U6/H2 라우팅; 데이터 검증기가 레벨업 풀 배선을 에러로 잡음).
			// 그 외 티어 없는 카드는 진짜 오설정이다. 어느 쪽이든 여기선 스킵+경고.
			UE_LOG(LogFPSR, Warning, TEXT("[Card] '%s' has no OfferRarities — skipped (level-up cards need a RarityTier; behavior fragments belong in UnlockableFeatures)."), *Card->GetName());
			continue;
		}
		for (const ECardRarity Rarity : Card->OfferRarities)
		{
			const float Weight = GetEffectiveWeight(Card, Rarity, Luck);
			if (Weight <= 0.0f)
			{
				continue;
			}
			FFPSRCardDraw Offer;
			Offer.Card = Card;
			Offer.Rarity = Rarity;
			Offer.TargetWeapon = SourceWeapon; // Weapon-group card → its source weapon; character/all-weapons → null
			Candidates.Add(Offer);
			CandidateWeights.Add(Weight);
		}
	}

	// Weighted sampling without replacement — extracted to a shared static helper (CRIT2) so the mission/unlock
	// pool (DrawWeaponUnlockOffer) can reuse the exact same non-replacement contract instead of a second hand-rolled
	// copy (CRIT1 교훈: 치명타 굴림 5중 복붙). GroupIds/BaselineWeights passed empty here -> the helper's plain
	// single-stage weighted pick, so this pool's behavior/RNG-call sequence is unchanged (§12-5 회귀 기준: 코드
	// 무접촉과 동급 — GetEffectiveWeight 도, 이 풀의 추첨 산식도 그대로다).
	//
	// Once an offer is picked, the exact same (card, rolled rarity) candidate is removed (a card can never repeat at
	// the SAME rarity it was just drawn at) and every remaining candidate sharing its (family, rolled rarity) pair is
	// also removed (CARDDRAW v4, Docs/Specs/CARDDRAW_FamilyRarityExclusion.md §5). A different rarity of the SAME
	// family — including the same card asset re-offered at another rarity — stays eligible: same family + different
	// rarity is a valid co-presence (the old same-card-pointer-at-any-rarity block is superseded by this pair key,
	// not layered on top of it).
	TArray<FFPSRCardDraw> Result;
	TArray<float> NoBaselineWeights; // empty -> ungrouped path (§6: "GroupIds 가 비어 있으면 종래의 단순 가중 추출")
	TArray<int32> NoGroupIds;
	WeightedSampleWithoutReplacement(Candidates, CandidateWeights, NoBaselineWeights, NoGroupIds, Count,
		[](const FFPSRCardDraw& Selected, const FFPSRCardDraw& Candidate) -> bool
		{
			const FName FamilyKey = GetCardFamilyKey(Selected.Card);
			// The exclusion key carries the TARGET WEAPON dimension (사용자 확정 2026-08-13): the same shared card
			// offered for two different owned weapons is two distinct choices ("라이플 연사 vs SMG 연사"), so only
			// candidates aimed at the SAME weapon collide. TargetWeapon is null for character/all-weapons offers,
			// where null==null keeps the plain (family, rarity) semantics.
			const bool bSameWeapon = (Candidate.TargetWeapon == Selected.TargetWeapon);
			// Unconditional same-card guard (레드팀 P2, 2026-08-13): single-effect cards may ship CardFamily=None
			// (IsDataValid allows it), so the family pair-key alone does not subsume the old bSameCard block.
			// When the family IS set, this is implied by bSameFamilyAndRarity below.
			const bool bSameCardSameRarity = (Candidate.Card == Selected.Card)
				&& (Candidate.Rarity == Selected.Rarity)
				&& bSameWeapon;
			const bool bSameFamilyAndRarity = (FamilyKey != NAME_None)
				&& (GetCardFamilyKey(Candidate.Card) == FamilyKey)
				&& (Candidate.Rarity == Selected.Rarity)
				&& bSameWeapon;
			return bSameCardSameRarity || bSameFamilyAndRarity;
		},
		Result);

	return Result;
}

bool UFPSRCardSubsystem::ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, EFPSROfferType OfferType, int32 ReplaceFragmentIndex)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	UFPSRCardDataAsset* Card = Draw.Card;
	if (!Card || !ForPlayer)
	{
		return false;
	}

	AFPSRPlayerState* PS = ForPlayer->GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return false;
	}

	UFPSRAbilitySystemComponent* ASC = PS->GetFPSRAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	// Gate by offer type: require the matching pending pick BEFORE applying so a stale/duplicate path can't
	// grant a card for free. Opening-seed applies without consuming anything.
	if (OfferType == EFPSROfferType::LevelUp && PS->GetCardPicksPending() <= 0)
	{
		return false;
	}
	if (OfferType == EFPSROfferType::WeaponUnlock && PS->GetWeaponUnlockPicksPending() <= 0)
	{
		return false;
	}

	// Build the server-side effect context (never replicated). The pawn's inventory + the draw's TargetWeapon let
	// weapon effects resolve their target; an unowned TargetWeapon resolves to null (anti-cheat: the offer was
	// server-built from owned weapons), so a forged target is rejected in pass 1 below.
	FFPSRCardEffectContext EffCtx;
	EffCtx.Player = ForPlayer;
	EffCtx.PS = PS;
	EffCtx.ASC = ASC;
	if (APawn* Pawn = ForPlayer->GetPawn())
	{
		EffCtx.Inventory = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	}
	EffCtx.TargetWeapon = Draw.TargetWeapon;
	EffCtx.ReplaceFragmentIndex = ReplaceFragmentIndex;

	if (Card->Effects.Num() == 0)
	{
		// Misconfigured card (IsDataValid guards authoring) — reject so the pick is NOT consumed.
		return false;
	}

	// Pass 1: every effect must be applicable (CanApply = complete precondition: ASC / inventory / target instance
	// resolvable). If any can't, reject WITHOUT consuming — preserves the v1 "no weapon/instance -> offer stays up"
	// contract and makes the apply transactional (single-threaded server: no yield between passes, so no
	// partial-apply-then-fail).
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
	{
		if (Effect && !Effect->CanApply(EffCtx))
		{
			UE_LOG(LogFPSR, Verbose, TEXT("[Card] ApplyCard '%s' rejected: %s cannot apply (pick not consumed)."),
				*Card->GetName(), *Effect->GetClass()->GetName());
			return false;
		}
	}

	// Pass 2: apply each effect with its OWN rolled-rarity magnitude (so multi-effect trade-offs scale independently).
	// No effect fails here — CanApply was the complete gate. Effect-type-agnostic: a new effect type needs no edit here.
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
	{
		if (!Effect)
		{
			continue;
		}
		const float Magnitude = Effect->ResolveMagnitude(Draw.Rarity);
		Effect->Apply(EffCtx, Magnitude);
		UE_LOG(LogFPSR, Log, TEXT("[Card] '%s': applied %s (mag %.2f)"),
			*Card->GetName(), *Effect->GetClass()->GetName(), Magnitude);
	}

	// Consume the matching pick.
	if (OfferType == EFPSROfferType::LevelUp)
	{
		PS->ConsumeCardPick();
	}
	else if (OfferType == EFPSROfferType::WeaponUnlock)
	{
		PS->ConsumeWeaponUnlockPick();
	}

	// CRIT2 §7 기록 시점 계약(익스플로잇 차단): 원장은 효과가 성공적으로 적용된 뒤에만 는다 — 픽 소비 뒤, return
	// true 앞. 제시만 받은 카드·거부된 픽·리롤로 버린 카드는 여기 도달하지 않으므로 기록되지 않는다. 이 서브시스템
	// 안이 유일 지점이다(디버그 FPSR.ApplyCard·교체 경로도 이 함수를 타므로 새어 나가지 않는다).
	PS->RecordAcquiredCard(Card, Draw.Rarity, Draw.TargetWeapon);

	return true;
}

FFPSRCardDraw UFPSRCardSubsystem::BuildSingleDraw(UFPSRCardDataAsset* Card, AController* ForPlayer) const
{
	FFPSRCardDraw Draw;
	if (!Card)
	{
		return Draw;
	}
	if (Card->OfferRarities.Num() == 0)
	{
		// Pure-behavior card (no numeric tiers, e.g. a fragment): there is no meaningful rarity to roll, but it must
		// still build a VALID draw so offer flows can present it. Default the rarity to Common.
		Draw.Card = Card;
		Draw.Rarity = ECardRarity::Common;
		return Draw;
	}

	float Luck = 0.0f;
	if (AFPSRPlayerState* PS = ForPlayer ? ForPlayer->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (UFPSRCombatSet* CombatSet = PS->GetCombatSet())
		{
			Luck = CombatSet->GetLuck();
		}
	}

	// Weighted pick among the card's offered rarities by effective weight (rarity base * luck), like DrawCards.
	float TotalWeight = 0.0f;
	for (const ECardRarity Rarity : Card->OfferRarities)
	{
		TotalWeight += GetEffectiveWeight(Card, Rarity, Luck);
	}
	ECardRarity Chosen = Card->OfferRarities[0];
	if (TotalWeight > 0.0f)
	{
		const float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		for (const ECardRarity Rarity : Card->OfferRarities)
		{
			Cumulative += GetEffectiveWeight(Card, Rarity, Luck);
			if (Pick <= Cumulative)
			{
				Chosen = Rarity;
				break;
			}
		}
	}

	Draw.Card = Card;
	Draw.Rarity = Chosen;
	return Draw;
}

float UFPSRCardSubsystem::GetUnlockDrawWeight(const UFPSRCardDataAsset* Card, const TMap<FName, int32>& TagCounts) const
{
	// 🔴 시너지가 곱해지는 유일한 지점(§6) — 미션/해금 풀 전용. 레벨업 풀은 GetEffectiveWeight 를 그대로 쓰고
	// 이 함수를 부르지 않는다.
	if (!Card)
	{
		return 0.0f;
	}
	if (!ActivePool)
	{
		return Card->Weight; // §7 계약: 풀 null -> Card->Weight(시너지 계수 없이 원 가중치로 폴백)
	}
	const float Synergy = ComputeSynergyMultiplier(Card->BuildTags, TagCounts, ActivePool->SynergyBonusPerCard, ActivePool->SynergyMaxStacks);
	return Card->Weight * Synergy;
}

TArray<FFPSRCardDraw> UFPSRCardSubsystem::DrawWeaponUnlockOffer(AController* ForPlayer, int32 Count)
{
	TArray<FFPSRCardDraw> Result;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !ActivePool)
	{
		return Result;
	}

	APawn* Pawn = ForPlayer ? ForPlayer->GetPawn() : nullptr;
	UFPSRWeaponInventoryComponent* Inv = Pawn ? Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
	if (!Inv)
	{
		return Result;
	}

	// CRIT2 §7: 원장에서 빌드 태그별 개수를 뽑아온다 — 이 추첨의 유일한 시너지 입력. PS 가 없으면(방어적) 빈 맵으로
	// 진행 = 시너지 1.0(무효과) = 종전 균등 거동과 동일(§12-6 회귀 기준).
	AFPSRPlayerState* PS = ForPlayer ? ForPlayer->GetPlayerState<AFPSRPlayerState>() : nullptr;
	TMap<FName, int32> TagCounts;
	if (PS)
	{
		PS->BuildTagCountMap(TagCounts);
	}

	// Cheap early-out only. Whether a SPECIFIC weapon fits is decided per candidate below, because slots are typed
	// (ranged 1-2, melee 3) — "a slot is free" and "this rifle has somewhere to go" are different questions.
	const bool bHasAnyFreeSlot = Inv->HasAnyFreeSlot();
	const TArray<UFPSRWeaponDataAsset*> Owned = Inv->GetOwnedWeapons();

	// Parallel candidate arrays: TargetWeapon is null for brand-new-weapon cards, the owned weapon for feature unlocks.
	TArray<UFPSRCardDataAsset*> Candidates;
	TArray<UFPSRWeaponDataAsset*> CandidateWeapons;

	// Part A — new-weapon candidates (WeaponUnlockCards): free slot + not already owned, de-duped by granted weapon.
	if (bHasAnyFreeSlot)
	{
		TArray<UFPSRWeaponDataAsset*> GrantedSeen;
		GrantedSeen.Reserve(ActivePool->WeaponUnlockCards.Num());
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : ActivePool->WeaponUnlockCards)
		{
			if (!Card)
			{
				continue;
			}
			UFPSRWeaponDataAsset* Granted = nullptr;
			for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
			{
				if (const UCardEffect_GrantWeapon* Grant = Cast<UCardEffect_GrantWeapon>(Effect))
				{
					Granted = Grant->WeaponToGrant;
					break;
				}
			}
			if (!Granted || Owned.Contains(Granted) || GrantedSeen.Contains(Granted))
			{
				continue;
			}
			// Per-candidate slot test, not the outer one: with both ranged slots full and only the melee slot open,
			// a rifle unlock would otherwise be offered and then grant nothing (AddWeapon returns INDEX_NONE),
			// silently costing the player their pick.
			if (!Inv->HasFreeSlotFor(Granted->GetArchetype()))
			{
				continue;
			}
			Candidates.Add(Card.Get());
			CandidateWeapons.Add(nullptr);
			GrantedSeen.Add(Granted);
		}
	}

	// Part B — feature-unlock candidates: each owned weapon's UnlockableFeatures. Behavior features are stack-gated
	// (skip when the fragment is maxed on that weapon); stat-only features (no fragment) are always offered.
	for (UFPSRWeaponDataAsset* Weapon : Owned)
	{
		if (!Weapon)
		{
			continue;
		}
		UFPSRWeaponInstance* Instance = Inv->GetInstanceForWeapon(Weapon);
		if (!Instance)
		{
			continue;
		}
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : Weapon->UnlockableFeatures)
		{
			if (!Card)
			{
				continue;
			}
			if (UFPSRWeaponFragment* Frag = GetCardBehaviorFragment(Card))
			{
				const int32 Stacks = Instance->GetFragmentStackCount(Frag);
				if (Stacks >= FMath::Max(Frag->MaxStacks, 1))
				{
					continue; // maxed on this weapon — skip
				}
				// A brand-new distinct fragment on a weapon already at its slot cap would need the (deferred) replacement
				// UI to choose what to drop. Skip it from the auto-offer so a plain ServerSelectCard pick can't bounce
				// (CanApply rejects an at-cap new fragment with no replace index) and strand the card-selection freeze
				// (U6). The deliberate swap path (ServerSelectCardReplacement) re-introduces such a fragment with a drop
				// index. Stacking an already-held fragment (Stacks > 0) is unaffected — it consumes no new slot.
				if (Stacks == 0 && Instance->IsAtFragmentSlotCap())
				{
					continue;
				}
			}
			// De-dup on (card, weapon).
			bool bAlready = false;
			for (int32 i = 0; i < Candidates.Num(); ++i)
			{
				if (Candidates[i] == Card && CandidateWeapons[i] == Weapon)
				{
					bAlready = true;
					break;
				}
			}
			if (!bAlready)
			{
				Candidates.Add(Card.Get());
				CandidateWeapons.Add(Weapon);
			}
		}
	}

	// CRIT2: 그룹 비중 보존 2단 추출(사용자 결정 C, §6)이 종전 균등 Fisher-Yates 셔플을 대체한다. A(새 무기,
	// Pool->WeaponUnlockCards 출신)/B(기능 카드, 무기 UnlockableFeatures 출신) 그룹 라벨은 CandidateWeapons[i]가
	// null인지로 그대로 판별된다(Part A는 항상 null, Part B는 항상 그 무기) — 위 후보 수집 루프는 그대로 둔다
	// (§6 "후보 수집은 현행 그대로"). 배제 술어 = 없음(헬퍼 계약의 자기 제거만) — 현행 수집이 이미 (카드,무기)로만
	// 디듑하므로, 같은 카드가 무기만 달리해 한 오퍼에 공존할 수 있고 그것이 §2-3-2 v4 의 의도다("같은 카드 포인터
	// 제거"로 구현하면 그 의도를 깬다, G1 P2-2).
	constexpr int32 GroupId_NewWeapon = 0;
	constexpr int32 GroupId_Feature = 1;

	const int32 NumCandidates = Candidates.Num();
	TArray<FFPSRCardDraw> WeightedCandidates;
	TArray<float> Weights;
	TArray<float> BaselineWeights;
	TArray<int32> GroupIds;
	WeightedCandidates.Reserve(NumCandidates);
	Weights.Reserve(NumCandidates);
	BaselineWeights.Reserve(NumCandidates);
	GroupIds.Reserve(NumCandidates);
	for (int32 i = 0; i < NumCandidates; ++i)
	{
		UFPSRCardDataAsset* Card = Candidates[i];
		FFPSRCardDraw Entry;
		Entry.Card = Card; // Rarity는 아직 굴리지 않은 기본값(Common) — 술어가 읽지 않으므로 무해하다(§6)
		Entry.TargetWeapon = CandidateWeapons[i];
		WeightedCandidates.Add(Entry);
		// BaselineWeights[i] = Card->Weight(시너지 제외) · Weights[i] = GetUnlockDrawWeight(시너지 포함, §6).
		// A 그룹(새 무기 카드)은 BuildTags 를 달지 않으므로 시너지가 1.0 -> 둘이 자동으로 같다.
		BaselineWeights.Add(Card ? Card->Weight : 0.0f);
		Weights.Add(GetUnlockDrawWeight(Card, TagCounts));
		GroupIds.Add(CandidateWeapons[i] == nullptr ? GroupId_NewWeapon : GroupId_Feature);
	}

	TArray<FFPSRCardDraw> Picked;
	WeightedSampleWithoutReplacement(WeightedCandidates, Weights, BaselineWeights, GroupIds, Count,
		[](const FFPSRCardDraw&, const FFPSRCardDraw&) -> bool { return false; }, // 배제 술어 없음(자기 제거만)
		Picked);

	// 현행 순서 보존(G1 P2-2): 선택 후 레어도를 굴린다. TargetWeapon 은 선택 시점의 값을 이월한다(현행 :515 동치,
	// G1 P3-6) — BuildSingleDraw 는 오퍼가 어느 무기용인지 모르므로 직접 넣어야 한다.
	for (const FFPSRCardDraw& PickedEntry : Picked)
	{
		FFPSRCardDraw Draw = BuildSingleDraw(PickedEntry.Card, ForPlayer);
		if (Draw.Card)
		{
			Draw.TargetWeapon = PickedEntry.TargetWeapon;
			Result.Add(Draw);
		}
	}
	return Result;
}

bool UFPSRCardSubsystem::TryReroll(AController* ForPlayer)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	AFPSRPlayerState* PS = ForPlayer ? ForPlayer->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (!PS)
	{
		return false;
	}

	return PS->ConsumeRerollCharge();
}

float UFPSRCardSubsystem::GetEffectiveWeight(const UFPSRCardDataAsset* Card, ECardRarity Rarity, float Luck) const
{
	if (!Card || !ActivePool)
	{
		return 0.0f;
	}

	const float RarityBase = ActivePool->GetRarityBaseWeight(Rarity);

	// Apply per-rarity luck increments: Luck adds to the base rarity weight.
	const float EffectiveRarityWeight = RarityBase + Luck * ActivePool->GetLuckPerRarity(Rarity);

	const float FinalWeight = Card->Weight * FMath::Max(EffectiveRarityWeight, 0.0f);
	return FMath::Max(FinalWeight, 0.0f);
}

void UFPSRCardSubsystem::GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates, TArray<UFPSRWeaponDataAsset*>& OutSourceWeapons) const
{
	OutCandidates.Reset();
	OutSourceWeapons.Reset();
	if (!ActivePool)
	{
		return;
	}

	// Central pool = character + all-weapons cards. These have no source weapon (TargetWeapon stays null):
	// character cards apply to the ASC, all-weapons cards apply to the PlayerState (every weapon).
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : ActivePool->Cards)
	{
		if (Card)
		{
			OutCandidates.Add(Card.Get());
			OutSourceWeapons.Add(nullptr);
		}
	}

	// Each OWNED weapon contributes its own WeaponCards (dynamic pool join, §2-4). A weapon-scope (ThisWeapon)
	// card from weapon W targets W specifically — so it applies to that weapon even when another is equipped, and
	// a weapon (e.g. melee) never offers stat cards it can't use. Character-scope cards a weapon carries apply to
	// the player (no target). The same card asset shared by two weapons yields two offers (one per target weapon).
	APawn* PlayerPawn = ForPlayer ? ForPlayer->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	UFPSRWeaponInventoryComponent* InventoryComp = PlayerPawn->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	if (!InventoryComp)
	{
		return;
	}

	TArray<UFPSRWeaponDataAsset*> OwnedWeapons = InventoryComp->GetOwnedWeapons();
	for (UFPSRWeaponDataAsset* Weapon : OwnedWeapons)
	{
		if (!Weapon)
		{
			continue;
		}
		for (UFPSRCardDataAsset* Card : Weapon->WeaponCards)
		{
			if (!Card)
			{
				continue;
			}
			// Weapon-group card → target this weapon. Character-group card carried by a weapon → no target.
			UFPSRWeaponDataAsset* Target = (Card->Group == ECardGroup::Weapon) ? Weapon : nullptr;

			// De-dup on (card, target): the same card may legitimately appear for different target weapons.
			bool bAlready = false;
			for (int32 i = 0; i < OutCandidates.Num(); ++i)
			{
				if (OutCandidates[i] == Card && OutSourceWeapons[i] == Target)
				{
					bAlready = true;
					break;
				}
			}
			if (!bAlready)
			{
				OutCandidates.Add(Card);
				OutSourceWeapons.Add(Target);
			}
		}
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	/** GC-safe cache of the most recent debug draw (weak card ref + rolled rarity/magnitude). */
	struct FDebugCardOffer
	{
		TWeakObjectPtr<UFPSRCardDataAsset> Card;
		ECardRarity Rarity = ECardRarity::Common;
		TWeakObjectPtr<UFPSRWeaponDataAsset> TargetWeapon; // preserve so FPSR.ApplyCard hits the right weapon
	};
	TArray<FDebugCardOffer> GLastDraw;

	APlayerController* GetLocalPC(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	void LogAndCacheDraw(const TArray<FFPSRCardDraw>& Draws)
	{
		GLastDraw.Reset();
		for (int32 i = 0; i < Draws.Num(); ++i)
		{
			if (!Draws[i].Card)
			{
				continue;
			}
			FDebugCardOffer Offer;
			Offer.Card = Draws[i].Card;
			Offer.Rarity = Draws[i].Rarity;
			Offer.TargetWeapon = Draws[i].TargetWeapon;
			GLastDraw.Add(Offer);

			const FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue((int64)Draws[i].Rarity);
			UE_LOG(LogFPSR, Log, TEXT("[Card] [%d] %s (%s)"), i, *Draws[i].Card->GetName(), *RarityStr);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GCmd_DrawCards(
		TEXT("FPSR.DrawCards"),
		TEXT("Draw N card offers for the local player (debug). Usage: FPSR.DrawCards [N]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>();
			APlayerController* PC = GetLocalPC(World);
			if (!CardSubsystem || !PC)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] DrawCards: subsystem or player controller not found"));
				return;
			}

			int32 DrawCount = 3;
			if (Args.Num() > 0)
			{
				DrawCount = FCString::Atoi(*Args[0]);
			}

			LogAndCacheDraw(CardSubsystem->DrawCards(PC, DrawCount));
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_ApplyCard(
		TEXT("FPSR.ApplyCard"),
		TEXT("Apply a card offer from the last draw (debug). Usage: FPSR.ApplyCard [index]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			APlayerController* PC = GetLocalPC(World);
			if (!PC)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] ApplyCard: player controller not found"));
				return;
			}

			int32 Index = 0;
			if (Args.Num() > 0)
			{
				Index = FCString::Atoi(*Args[0]);
			}

			if (!GLastDraw.IsValidIndex(Index) || !GLastDraw[Index].Card.IsValid())
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] ApplyCard: invalid index %d"), Index);
				return;
			}

			UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>();
			if (CardSubsystem)
			{
				FFPSRCardDraw Draw;
				Draw.Card = GLastDraw[Index].Card.Get();
				Draw.Rarity = GLastDraw[Index].Rarity;
				Draw.TargetWeapon = GLastDraw[Index].TargetWeapon.Get();
				// Debug apply as an opening-seed pick (no pending level-up required).
				const bool bApplied = CardSubsystem->ApplyCard(PC, Draw, EFPSROfferType::OpeningSeed);
				UE_LOG(LogFPSR, Log, TEXT("[Card] ApplyCard index %d -> %s"),
					Index, bApplied ? TEXT("applied") : TEXT("rejected"));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_Reroll(
		TEXT("FPSR.Reroll"),
		TEXT("Consume a reroll charge and redraw (debug). Usage: FPSR.Reroll [N]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>();
			APlayerController* PC = GetLocalPC(World);
			if (!CardSubsystem || !PC)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] Reroll: subsystem or player controller not found"));
				return;
			}

			if (!CardSubsystem->TryReroll(PC))
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] Reroll: not enough reroll charges"));
				return;
			}

			int32 DrawCount = 3;
			if (Args.Num() > 0)
			{
				DrawCount = FCString::Atoi(*Args[0]);
			}

			LogAndCacheDraw(CardSubsystem->DrawCards(PC, DrawCount));
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_RerollCharges(
		TEXT("FPSR.RerollCharges"),
		TEXT("Set reroll charges for the local player (debug). Usage: FPSR.RerollCharges [N]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			APlayerController* PC = GetLocalPC(World);
			if (!PC)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] RerollCharges: player controller not found"));
				return;
			}

			AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
			if (!PS)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] RerollCharges: player state not found"));
				return;
			}

			int32 NewCharges = 3;
			if (Args.Num() > 0)
			{
				NewCharges = FCString::Atoi(*Args[0]);
			}

			PS->SetRerollCharges(NewCharges);
			UE_LOG(LogFPSR, Log, TEXT("[Card] RerollCharges set to %d"), NewCharges);
		}));
}
#endif // !UE_BUILD_SHIPPING
