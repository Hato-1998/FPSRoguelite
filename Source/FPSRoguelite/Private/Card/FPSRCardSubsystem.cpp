// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"
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
	/** Family key used to keep same-family cards mutually exclusive in a single draw.
	 *  Prefers the explicit CardFamily tag; falls back to the AppliedEffect GE class. */
	FName GetCardFamilyKey(const UFPSRCardDataAsset* Card)
	{
		if (!Card)
		{
			return NAME_None;
		}
		if (Card->CardFamily.IsValid())
		{
			return Card->CardFamily.GetTagName();
		}
		// AppliedEffect is only meaningful (and only applied) for Character-scope cards, so use it as the family
		// fallback only there. Weapon-scope cards never apply a GE — a stale AppliedEffect (e.g. left over on a
		// Character asset copied/retargeted to a weapon scope) must not group them into an unrelated GE family.
		if (Card->Scope == ECardScope::Character && Card->AppliedEffect)
		{
			return Card->AppliedEffect->GetFName();
		}
		return NAME_None;
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
	bool bHasWeapon = false;
	if (APawn* Pawn = ForPlayer ? ForPlayer->GetPawn() : nullptr)
	{
		if (UFPSRWeaponInventoryComponent* Inv = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>())
		{
			bHasWeapon = Inv->GetOwnedWeapons().Num() > 0;
		}
	}

	// Flatten each eligible card into one weighted offer per rarity tier. Excluded cards, and weapon-scope
	// cards while the player owns no weapon, are skipped so they are never offered.
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
		const bool bWeaponScope = (Card->Scope == ECardScope::ThisWeapon || Card->Scope == ECardScope::AllWeapons);
		if (bWeaponScope && !bHasWeapon)
		{
			continue;
		}
		if (Card->RarityTiers.Num() == 0)
		{
			// Not silent: a misconfigured card with no tiers is logged rather than vanishing from the draw.
			UE_LOG(LogFPSR, Warning, TEXT("[Card] '%s' has no RarityTiers — skipped (configure at least one tier)."), *Card->GetName());
			continue;
		}
		for (const FFPSRCardRarityTier& Tier : Card->RarityTiers)
		{
			const float Weight = GetEffectiveWeight(Card, Tier.Rarity, Luck);
			if (Weight <= 0.0f)
			{
				continue;
			}
			FFPSRCardDraw Offer;
			Offer.Card = Card;
			Offer.Rarity = Tier.Rarity;
			Offer.Magnitude = Tier.Magnitude;
			Offer.TargetWeapon = SourceWeapon; // ThisWeapon card → its source weapon; central/character → null
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
	if (OfferType == EFPSROfferType::MissionReward && PS->GetMissionRewardPicksPending() <= 0)
	{
		return false;
	}

	// Apply the card's effect. Character-scope applies a GameplayEffect to the ASC; weapon-scope cards apply
	// a stat modifier to the weapon instance(s) (weapon stats live outside the ASC, §2-4-1).
	if (Card->Scope == ECardScope::Character)
	{
		if (Card->AppliedEffect)
		{
			FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
			Ctx.AddSourceObject(this);
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Card->AppliedEffect, 1.0f, Ctx);
			if (Spec.IsValid())
			{
				// For GEs whose modifier uses SetByCaller (tag SetByCaller.CardMagnitude). Harmless for fixed GEs.
				static const FGameplayTag CardMagnitudeTag = FGameplayTag::RequestGameplayTag(FName("SetByCaller.CardMagnitude"));
				Spec.Data->SetSetByCallerMagnitude(CardMagnitudeTag, Draw.Magnitude);
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}
	else // ThisWeapon / AllWeapons
	{
		APawn* Pawn = ForPlayer->GetPawn();
		UFPSRWeaponInventoryComponent* Inventory = Pawn ? Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
		if (!Inventory)
		{
			// No weapon system on this pawn — reject so the pick is NOT consumed (offer stays up).
			return false;
		}

		const FFPSRWeaponStatMod Mod{ Card->WeaponStat, Card->WeaponStatOp, Draw.Magnitude };
		if (Card->Scope == ECardScope::ThisWeapon)
		{
			// Apply to the weapon that contributed this offer (Draw.TargetWeapon), resolved to the player's
			// owned instance — so the card lands on its source weapon even if another is equipped. This also
			// gates anti-cheat: the offer was server-built from owned weapons, and an unowned TargetWeapon
			// resolves to null and is rejected. Legacy offers with no target fall back to the equipped weapon.
			UFPSRWeaponInstance* Instance = Draw.TargetWeapon
				? Inventory->GetInstanceForWeapon(Draw.TargetWeapon)
				: Inventory->GetCurrentInstance();
			if (!Instance)
			{
				return false;
			}
			if (Card->GrantedFragment)
			{
				// Behavior-fragment card (mission reward): attach the fragment to the target weapon.
				Instance->AddFragment(Card->GrantedFragment);
			}
			else
			{
				Instance->AddModifier(Mod);
			}
		}
		else // AllWeapons
		{
			PS->AddAllWeaponsModifier(Mod);
		}
	}

	// Consume the matching pick.
	if (OfferType == EFPSROfferType::LevelUp)
	{
		PS->ConsumeCardPick();
	}
	else if (OfferType == EFPSROfferType::MissionReward)
	{
		PS->ConsumeMissionRewardPick();
	}

	return true;
}

FFPSRCardDraw UFPSRCardSubsystem::BuildSingleDraw(UFPSRCardDataAsset* Card, AController* ForPlayer) const
{
	FFPSRCardDraw Draw;
	if (!Card || Card->RarityTiers.Num() == 0)
	{
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

	// Weighted pick among the card's tiers by effective weight (rarity base * luck), like DrawCards.
	float TotalWeight = 0.0f;
	for (const FFPSRCardRarityTier& Tier : Card->RarityTiers)
	{
		TotalWeight += GetEffectiveWeight(Card, Tier.Rarity, Luck);
	}
	const FFPSRCardRarityTier* Chosen = &Card->RarityTiers[0];
	if (TotalWeight > 0.0f)
	{
		const float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		for (const FFPSRCardRarityTier& Tier : Card->RarityTiers)
		{
			Cumulative += GetEffectiveWeight(Card, Tier.Rarity, Luck);
			if (Pick <= Cumulative)
			{
				Chosen = &Tier;
				break;
			}
		}
	}

	Draw.Card = Card;
	Draw.Rarity = Chosen->Rarity;
	Draw.Magnitude = Chosen->Magnitude;
	return Draw;
}

TArray<FFPSRCardDraw> UFPSRCardSubsystem::DrawWeaponModifierOffer(AController* ForPlayer, int32 Count)
{
	TArray<FFPSRCardDraw> Result;

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || Count <= 0)
	{
		return Result;
	}

	APawn* Pawn = ForPlayer ? ForPlayer->GetPawn() : nullptr;
	UFPSRWeaponInventoryComponent* Inv = Pawn ? Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
	if (!Inv)
	{
		return Result;
	}

	// Candidate fragment cards = every OWNED weapon's AvailableModifiers that the weapon's own instance still has
	// room to stack. Each candidate is tagged with its source weapon so the fragment applies to that weapon (not
	// whatever is equipped). A stackable fragment (e.g. MultiShot MaxStacks=2) keeps appearing until full.
	TArray<UFPSRCardDataAsset*> Candidates;
	TArray<UFPSRWeaponDataAsset*> CandidateWeapons; // index-aligned with Candidates
	for (UFPSRWeaponDataAsset* Weapon : Inv->GetOwnedWeapons())
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
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : Weapon->AvailableModifiers)
		{
			if (Card && Card->Scope == ECardScope::ThisWeapon && Card->GrantedFragment
				&& Instance->GetFragmentStackCount(Card->GrantedFragment) < FMath::Max(Card->GrantedFragment->MaxStacks, 1))
			{
				// Same card asset on two weapons yields two candidates (distinct target weapons).
				Candidates.Add(Card.Get());
				CandidateWeapons.Add(Weapon);
			}
		}
	}

	// Shuffle (Fisher–Yates, parallel arrays) so the offered subset varies, then take up to Count.
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
			Draw.TargetWeapon = CandidateWeapons[i]; // fragment applies to its source weapon
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
			// ThisWeapon card → target this weapon. Character/AllWeapons card carried by a weapon → no target.
			UFPSRWeaponDataAsset* Target = (Card->Scope == ECardScope::ThisWeapon) ? Weapon : nullptr;

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
		float Magnitude = 0.0f;
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
			Offer.Magnitude = Draws[i].Magnitude;
			Offer.TargetWeapon = Draws[i].TargetWeapon;
			GLastDraw.Add(Offer);

			const FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue((int64)Draws[i].Rarity);
			UE_LOG(LogFPSR, Log, TEXT("[Card] [%d] %s (%s, mag %.2f)"), i, *Draws[i].Card->GetName(), *RarityStr, Draws[i].Magnitude);
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
				Draw.Magnitude = GLastDraw[Index].Magnitude;
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
