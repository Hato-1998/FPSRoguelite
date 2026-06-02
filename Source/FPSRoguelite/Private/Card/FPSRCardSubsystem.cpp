// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
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
		if (Card->AppliedEffect)
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
	GatherCandidatePool(ForPlayer, Cards);

	// Player luck / rarity bonus shift the draw toward higher rarities.
	float Luck = 0.0f;
	float RarityBonus = 0.0f;
	if (AFPSRPlayerState* PS = ForPlayer ? ForPlayer->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (UFPSRCombatSet* CombatSet = PS->GetCombatSet())
		{
			Luck = CombatSet->GetLuck();
			RarityBonus = CombatSet->GetRarityBonus();
		}
	}

	// Flatten each Character-scope card into one weighted offer per rarity tier. Weapon-scope cards (P4)
	// and excluded cards are skipped so they are never offered.
	TArray<FFPSRCardDraw> Candidates;
	TArray<float> CandidateWeights;
	for (UFPSRCardDataAsset* Card : Cards)
	{
		if (!Card || Card->Scope != ECardScope::Character || Exclude.Contains(Card))
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
			const float Weight = GetEffectiveWeight(Card, Tier.Rarity, Luck, RarityBonus);
			if (Weight <= 0.0f)
			{
				continue;
			}
			FFPSRCardDraw Offer;
			Offer.Card = Card;
			Offer.Rarity = Tier.Rarity;
			Offer.Magnitude = Tier.Magnitude;
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

bool UFPSRCardSubsystem::ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, bool bConsumeLevelUp)
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

	// Weapon-scope cards modify weapon stats, which is not implemented until P4. Reject without
	// spending the player's selection so a weapon card can never be wasted on a no-op.
	if (Card->Scope != ECardScope::Character)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Card] Rejected weapon-scope card '%s' — weapon-modifier application is P4"), *Card->GetName());
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

	// When this apply represents a breather level-up selection, require a queued level-up and consume it
	// atomically — gate before applying the effect so a stale/duplicate path can't grant cards for free.
	// Opening-seed selections (§2-2) pass bConsumeLevelUp=false and skip this gate.
	AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
	if (bConsumeLevelUp)
	{
		if (!GS || GS->GetPendingLevelUps() <= 0)
		{
			return false;
		}
	}

	// Apply the card's GameplayEffect to the player ASC (Character scope), injecting the rolled tier magnitude.
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

	if (bConsumeLevelUp && GS)
	{
		GS->ConsumePendingLevelUp();
	}

	return true;
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

float UFPSRCardSubsystem::GetEffectiveWeight(const UFPSRCardDataAsset* Card, ECardRarity Rarity, float Luck, float RarityBonus) const
{
	if (!Card || !ActivePool)
	{
		return 0.0f;
	}

	const float RarityBase = ActivePool->GetRarityBaseWeight(Rarity);

	// Higher rarity tiers are boosted by luck / rarity bonus (tier index Common=0 .. Legendary=3).
	const int32 RarityTier = static_cast<int32>(Rarity);
	float LuckBoost = 1.0f + (RarityBonus * ActivePool->RarityBonusScale + Luck * ActivePool->LuckScale) * static_cast<float>(RarityTier);
	LuckBoost = FMath::Max(LuckBoost, 0.0f);

	const float FinalWeight = Card->Weight * RarityBase * LuckBoost;
	return FMath::Max(FinalWeight, 0.0f);
}

void UFPSRCardSubsystem::GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates) const
{
	if (!ActivePool)
	{
		return;
	}

	OutCandidates.Reset();
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : ActivePool->Cards)
	{
		if (Card)
		{
			OutCandidates.Add(Card.Get());
		}
	}

	// Add cards from the player's owned weapons (dynamic pool join, §2-4).
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
			if (Card && !OutCandidates.Contains(Card))
			{
				OutCandidates.Add(Card);
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
				const bool bApplied = CardSubsystem->ApplyCard(PC, Draw);
				UE_LOG(LogFPSR, Log, TEXT("[Card] ApplyCard index %d -> %s (needs a pending level-up; FPSR.AddXP to queue one)"),
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
