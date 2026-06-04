// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Core/FPSRLogChannels.h"

AFPSRGameState::AFPSRGameState()
{
}

void AFPSRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, SharedXP, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, PartyLevel, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunPhase, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, CurrentRound, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, BankedMissionRewards, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunClockSeconds, Params);
}

int32 AFPSRGameState::GetRequiredXP(int32 Level) const
{
	return XPBaseRequired + FMath::Max(0, Level - 1) * XPPerLevel;
}

void AFPSRGameState::AddSharedXP(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return;
	}

	SharedXP += Amount;
	int32 LevelsGained = 0;
	while (SharedXP >= GetRequiredXP(PartyLevel))
	{
		SharedXP -= GetRequiredXP(PartyLevel);
		++PartyLevel;
		++LevelsGained;
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, SharedXP, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, PartyLevel, this);

	// Grant each connected player one pending card pick per level gained.
	if (LevelsGained > 0)
	{
		for (APlayerState* PS : PlayerArray)
		{
			if (AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS))
			{
				for (int32 i = 0; i < LevelsGained; ++i)
				{
					FPS->AddCardPick();
				}
			}
		}

		// If picks are granted while already in the breather, present them now (don't wait for a re-entry).
		if (RunPhase == ERunPhase::Breather)
		{
			PresentPendingLevelUpOffers();
		}
	}

	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::SetRunPhase(ERunPhase NewPhase)
{
	if (!HasAuthority() || RunPhase == NewPhase)
	{
		return;
	}
	RunPhase = NewPhase;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunPhase, this);
	OnRunStateChanged.Broadcast();

	// Entering the breather: present a level-up card offer to every player with pending picks (§2-2).
	if (NewPhase == ERunPhase::Breather)
	{
		PresentPendingLevelUpOffers();
	}
}

void AFPSRGameState::PresentPendingLevelUpOffers()
{
	if (!HasAuthority())
	{
		return;
	}

	for (APlayerState* PS : PlayerArray)
	{
		AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
		if (!FPS || FPS->GetCardPicksPending() <= 0)
		{
			continue;
		}

		AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(FPS->GetOwningController());
		if (PC && !PC->HasActiveOffer())
		{
			PC->RequestCardOffer(true);
		}
	}
}

void AFPSRGameState::SetCurrentRound(int32 NewRound)
{
	if (!HasAuthority() || CurrentRound == NewRound)
	{
		return;
	}
	CurrentRound = NewRound;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, CurrentRound, this);
	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::AddBankedMissionReward(int32 Count)
{
	if (!HasAuthority() || Count <= 0)
	{
		return;
	}
	BankedMissionRewards += Count;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, BankedMissionRewards, this);
	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::ResetBankedMissionRewards()
{
	if (!HasAuthority() || BankedMissionRewards == 0)
	{
		return;
	}
	BankedMissionRewards = 0;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, BankedMissionRewards, this);
	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::SetRunClockSeconds(float Seconds)
{
	if (!HasAuthority())
	{
		return;
	}
	// Low-frequency UI mirror: only dirty on a meaningful change to avoid per-tick replication churn.
	if (FMath::IsNearlyEqual(RunClockSeconds, Seconds, 0.25f))
	{
		return;
	}
	RunClockSeconds = Seconds;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunClockSeconds, this);
}

void AFPSRGameState::OnRep_RunState()
{
	OnRunStateChanged.Broadcast();
}

#if !UE_BUILD_SHIPPING

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCmd_AddXP(
		TEXT("FPSR.AddXP"),
		TEXT("Add shared XP to the run (debug). Usage: FPSR.AddXP [amount]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
			if (!GS)
			{
				return;
			}
			int32 Amount = 50;
			if (Args.Num() > 0)
			{
				Amount = FMath::Max(1, FCString::Atoi(*Args[0]));
			}
			GS->AddSharedXP(Amount);
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_SetPhase(
		TEXT("FPSR.SetPhase"),
		TEXT("Set run phase: combat|breather (or 0|1). Usage: FPSR.SetPhase [phase]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
			if (!GS)
			{
				return;
			}
			ERunPhase Phase = ERunPhase::Combat;
			if (Args.Num() > 0)
			{
				const FString A = Args[0].ToLower();
				Phase = (A == TEXT("breather") || A == TEXT("1")) ? ERunPhase::Breather : ERunPhase::Combat;
			}
			GS->SetRunPhase(Phase);
		}));
}

#endif // !UE_BUILD_SHIPPING
