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
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

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

TArray<UFPSRCardDataAsset*> UFPSRCardSubsystem::DrawCards(AController* ForPlayer, int32 Count, const TArray<UFPSRCardDataAsset*>& Exclude)
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

	TArray<UFPSRCardDataAsset*> Result;
	TArray<UFPSRCardDataAsset*> Candidates;
	GatherCandidatePool(ForPlayer, Candidates);

	// Remove nulls, excluded cards, and not-yet-applicable weapon-scope cards (P4) so they are never offered.
	for (int32 i = Candidates.Num() - 1; i >= 0; --i)
	{
		if (!Candidates[i] || Candidates[i]->Scope != ECardScope::Character || Exclude.Contains(Candidates[i]))
		{
			Candidates.RemoveAt(i);
		}
	}

	// Get player luck and rarity bonus from CombatSet.
	float Luck = 0.0f;
	float RarityBonus = 0.0f;
	if (AFPSRPlayerState* PS = ForPlayer->GetPlayerState<AFPSRPlayerState>())
	{
		if (UFPSRCombatSet* CombatSet = PS->GetCombatSet())
		{
			Luck = CombatSet->GetLuck();
			RarityBonus = CombatSet->GetRarityBonus();
		}
	}

	// Weighted sampling without replacement.
	for (int32 i = 0; i < Count && Candidates.Num() > 0; ++i)
	{
		float TotalWeight = 0.0f;
		TArray<float> Weights;
		Weights.Reserve(Candidates.Num());

		for (const UFPSRCardDataAsset* Card : Candidates)
		{
			float Weight = GetEffectiveWeight(Card, Luck, RarityBonus);
			Weights.Add(Weight);
			TotalWeight += Weight;
		}

		if (TotalWeight <= 0.0f)
		{
			break;
		}

		float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		int32 SelectedIndex = 0;

		for (int32 j = 0; j < Weights.Num(); ++j)
		{
			Cumulative += Weights[j];
			if (Pick <= Cumulative)
			{
				SelectedIndex = j;
				break;
			}
		}

		if (Candidates.IsValidIndex(SelectedIndex))
		{
			Result.Add(Candidates[SelectedIndex]);
			Candidates.RemoveAt(SelectedIndex);
		}
	}

	return Result;
}

bool UFPSRCardSubsystem::ApplyCard(AController* ForPlayer, UFPSRCardDataAsset* Card, bool bConsumeLevelUp)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

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

	// Apply the card's GameplayEffect to the player ASC (Character scope).
	if (Card->AppliedEffect)
	{
		FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
		Ctx.AddSourceObject(this);
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Card->AppliedEffect, 1.0f, Ctx);
		if (Spec.IsValid())
		{
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

	AFPSRPlayerState* PS = ForPlayer->GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return false;
	}

	return PS->ConsumeRerollCharge();
}

float UFPSRCardSubsystem::GetEffectiveWeight(const UFPSRCardDataAsset* Card, float Luck, float RarityBonus) const
{
	if (!Card || !ActivePool)
	{
		return 0.0f;
	}

	float RarityBase = ActivePool->GetRarityBaseWeight(Card->Rarity);

	// Map rarity to a tier index for luck scaling.
	int32 RarityTier = static_cast<int32>(Card->Rarity);
	float LuckBoost = 1.0f + (RarityBonus * ActivePool->RarityBonusScale + Luck * ActivePool->LuckScale) * static_cast<float>(RarityTier);
	LuckBoost = FMath::Max(LuckBoost, 0.0f);

	float FinalWeight = Card->Weight * RarityBase * LuckBoost;
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

	// Add cards from player's equipped weapons.
	APawn* PlayerPawn = ForPlayer->GetPawn();
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
	TArray<TWeakObjectPtr<UFPSRCardDataAsset>> GLastDraw;

	APlayerController* GetLocalPC(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GCmd_DrawCards(
		TEXT("FPSR.DrawCards"),
		TEXT("Draw N cards for the local player (debug). Usage: FPSR.DrawCards [N]"),
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

			GLastDraw.Reset();
			TArray<UFPSRCardDataAsset*> DrawnCards = CardSubsystem->DrawCards(PC, DrawCount);

			for (int32 i = 0; i < DrawnCards.Num(); ++i)
			{
				UFPSRCardDataAsset* Card = DrawnCards[i];
				if (Card)
				{
					GLastDraw.Add(Card);
					FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue((int64)Card->Rarity);
					UE_LOG(LogFPSR, Log, TEXT("[Card] [%d] %s (%s)"), i, *Card->GetName(), *RarityStr);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_ApplyCard(
		TEXT("FPSR.ApplyCard"),
		TEXT("Apply a card from the last draw (debug). Usage: FPSR.ApplyCard [index]"),
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

			if (!GLastDraw.IsValidIndex(Index) || !GLastDraw[Index].IsValid())
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Card] ApplyCard: invalid index %d"), Index);
				return;
			}

			UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>();
			if (CardSubsystem)
			{
				const bool bApplied = CardSubsystem->ApplyCard(PC, GLastDraw[Index].Get());
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

			GLastDraw.Reset();
			TArray<UFPSRCardDataAsset*> DrawnCards = CardSubsystem->DrawCards(PC, DrawCount);

			for (int32 i = 0; i < DrawnCards.Num(); ++i)
			{
				UFPSRCardDataAsset* Card = DrawnCards[i];
				if (Card)
				{
					GLastDraw.Add(Card);
					FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue((int64)Card->Rarity);
					UE_LOG(LogFPSR, Log, TEXT("[Card] [%d] %s (%s)"), i, *Card->GetName(), *RarityStr);
				}
			}
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
#endif
