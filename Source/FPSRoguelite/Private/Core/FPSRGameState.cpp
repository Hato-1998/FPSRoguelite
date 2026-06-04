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
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, bRunPaused, Params);
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

	// Each level grants every connected player one pending card pick.
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

		// Level-up freezes the run immediately and presents cards to everyone (Game.MD §2-2).
		RefreshPauseState();
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
}

void AFPSRGameState::SetRunPaused(bool bPaused)
{
	if (!HasAuthority() || bRunPaused == bPaused)
	{
		return;
	}
	bRunPaused = bPaused;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, bRunPaused, this);
	OnRunStateChanged.Broadcast();

	UE_LOG(LogFPSR, Log, TEXT("[Run] %s"), bPaused ? TEXT("FREEZE (card selection)") : TEXT("RESUME"));
}

void AFPSRGameState::RefreshPauseState()
{
	if (!HasAuthority())
	{
		return;
	}

	// Paused iff any connected player still has an outstanding selection. Present the next needed offer to
	// each player that has picks but no active offer (covers newly granted picks).
	bool bAnyPending = false;
	for (APlayerState* PS : PlayerArray)
	{
		AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
		if (!FPS)
		{
			continue;
		}
		AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(FPS->GetOwningController());
		if (!PC)
		{
			continue;
		}
		PC->PresentNextOfferIfNeeded();
		if (PC->HasPendingSelection())
		{
			bAnyPending = true;
		}
	}

	SetRunPaused(bAnyPending);
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

	FAutoConsoleCommandWithWorldAndArgs GCmd_Pause(
		TEXT("FPSR.Pause"),
		TEXT("Toggle the global run freeze (debug). Usage: FPSR.Pause [0|1]"),
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
			bool bPaused = !GS->IsRunPaused();
			if (Args.Num() > 0)
			{
				bPaused = FCString::Atoi(*Args[0]) != 0;
			}
			GS->SetRunPaused(bPaused);
		}));
}

#endif // !UE_BUILD_SHIPPING
