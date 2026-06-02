// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameState.h"
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
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, PendingLevelUps, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunPhase, Params);
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
	while (SharedXP >= GetRequiredXP(PartyLevel))
	{
		SharedXP -= GetRequiredXP(PartyLevel);
		++PartyLevel;
		++PendingLevelUps;
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, SharedXP, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, PartyLevel, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, PendingLevelUps, this);
}

void AFPSRGameState::SetRunPhase(ERunPhase NewPhase)
{
	if (!HasAuthority() || RunPhase == NewPhase)
	{
		return;
	}
	RunPhase = NewPhase;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunPhase, this);
}

void AFPSRGameState::ConsumePendingLevelUp()
{
	if (!HasAuthority() || PendingLevelUps <= 0)
	{
		return;
	}
	--PendingLevelUps;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, PendingLevelUps, this);
}

void AFPSRGameState::OnRep_RunState()
{
	// Cosmetic hook for clients (HUD binds in P3-D).
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
