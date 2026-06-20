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
	/** Family key used to keep same-family cards mutually exclusive in a single draw. v2: the explicit CardFamily tag
	 *  only (the v1 AppliedEffect-GE-class fallback was removed — with polymorphic multi-effect cards there is no
	 *  single card-level GE to key on, and IsDataValid requires multi-effect cards to set CardFamily). */
	FName GetCardFamilyKey(const UFPSRCardDataAsset* Card)
	{
		if (!Card || !Card->CardFamily.IsValid())
		{
			return NAME_None;
		}
		return Card->CardFamily.GetTagName();
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
	const bool bHasWeapon = Inv && Inv->GetOwnedWeapons().Num() > 0;

	// Flatten each eligible card into one weighted offer per OFFERED rarity. Excluded cards, behavior-fragment
	// cards (mission-reward only), and weapon-targeting cards while the player owns no weapon are skipped.
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
		// Behavior-fragment cards now join the level-up draw (U18b routing). Stack gate: a fragment already maxed on its
		// target weapon is no longer offered (ported from the old mission path). SourceWeapon picks the target instance.
		if (UFPSRWeaponFragment* BehFrag = GetCardBehaviorFragment(Card))
		{
			UFPSRWeaponInstance* Inst = (Inv && SourceWeapon) ? Inv->GetInstanceForWeapon(SourceWeapon)
			                                                  : (Inv ? Inv->GetCurrentInstance() : nullptr);
			if (!Inst || Inst->GetFragmentStackCount(BehFrag) >= FMath::Max(BehFrag->MaxStacks, 1))
			{
				continue;
			}
		}
		// Weapon-targeting cards (this-weapon / all-weapons stat) join the pool only once a weapon is owned (v1 gate).
		if (CardRequiresWeapon(Card) && !bHasWeapon)
		{
			continue;
		}
		if (Card->OfferRarities.Num() == 0)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Card] '%s' has no OfferRarities — skipped (give an effect at least one RarityTier)."), *Card->GetName());
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

	// Weighted sampling without replacement. Once an offer is picked, every remaining offer of the same card
	// (all its tiers) and same family is removed, so a card never appears twice and families stay exclusive.
	TArray<FFPSRCardDraw> Result;
	for (int32 i = 0; i < Count && Candidates.Num() > 0; ++i)
	{
		float TotalWeight = 0.0f;
		for (const float W : CandidateWeights)
		{
			TotalWeight += W;
		}
		if (TotalWeight <= 0.0f)
		{
			break;
		}

		const float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		int32 SelectedIndex = 0;
		for (int32 j = 0; j < CandidateWeights.Num(); ++j)
		{
			Cumulative += CandidateWeights[j];
			if (Pick <= Cumulative)
			{
				SelectedIndex = j;
				break;
			}
		}

		if (!Candidates.IsValidIndex(SelectedIndex))
		{
			break;
		}

		const FFPSRCardDraw Selected = Candidates[SelectedIndex];
		Result.Add(Selected);

		const FName FamilyKey = GetCardFamilyKey(Selected.Card);
		for (int32 k = Candidates.Num() - 1; k >= 0; --k)
		{
			const bool bSameCard = (Candidates[k].Card == Selected.Card);
			const bool bSameFamily = (FamilyKey != NAME_None) && (GetCardFamilyKey(Candidates[k].Card) == FamilyKey);
			if (bSameCard || bSameFamily)
			{
				Candidates.RemoveAt(k);
				CandidateWeights.RemoveAt(k);
			}
		}
	}

	return Result;
}

bool UFPSRCardSubsystem::ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, EFPSROfferType OfferType)
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

	const bool bHasFreeSlot = Inv->HasFreeSlot();
	const TArray<UFPSRWeaponDataAsset*> Owned = Inv->GetOwnedWeapons();

	// Parallel candidate arrays: TargetWeapon is null for brand-new-weapon cards, the owned weapon for feature unlocks.
	TArray<UFPSRCardDataAsset*> Candidates;
	TArray<UFPSRWeaponDataAsset*> CandidateWeapons;

	// Part A — new-weapon candidates (WeaponUnlockCards): free slot + not already owned, de-duped by granted weapon.
	if (bHasFreeSlot)
	{
		TArray<UFPSRWeaponDataAsset*> GrantedSeen;
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
				if (Instance->GetFragmentStackCount(Frag) >= FMath::Max(Frag->MaxStacks, 1))
				{
					continue; // maxed on this weapon — skip
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

	// Shuffle parallel arrays in lockstep (Fisher-Yates) and take up to Count.
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Candidates.Swap(i, j);
		CandidateWeapons.Swap(i, j);
	}
	const int32 Take = FMath::Min(Count, Candidates.Num());
	for (int32 i = 0; i < Take; ++i)
	{
		FFPSRCardDraw Draw = BuildSingleDraw(Candidates[i], ForPlayer);
		if (Draw.Card)
		{
			Draw.TargetWeapon = CandidateWeapons[i];
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

	// Higher rarity tiers are boosted by player Luck (tier index Common=0 .. Legendary=3).
	const int32 RarityTier = static_cast<int32>(Rarity);
	float LuckBoost = 1.0f + (Luck * ActivePool->LuckScale) * static_cast<float>(RarityTier);
	LuckBoost = FMath::Max(LuckBoost, 0.0f);

	const float FinalWeight = Card->Weight * RarityBase * LuckBoost;
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
