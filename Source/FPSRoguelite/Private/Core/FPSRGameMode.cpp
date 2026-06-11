// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameMode.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "Hero/FPSRCharacter.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

AFPSRGameMode::AFPSRGameMode()
{
	GameStateClass = AFPSRGameState::StaticClass();
	PlayerControllerClass = AFPSRPlayerController::StaticClass();
	PlayerStateClass = AFPSRPlayerState::StaticClass();
	DefaultPawnClass = AFPSRCharacter::StaticClass();
}

void AFPSRGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>())
		{
			CardSubsystem->SetActivePool(CardPool);
		}

		// Configure the swarm enemy class (designer BP) before the run starts spawning.
		if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			SpawnSub->SetEnemyClass(EnemyClass);
		}

		// Start the run: hand the schedule to the director (server authority) and kick off round 0.
		if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
		{
			RunDirector->SetActiveSchedule(RunSchedule);
			RunDirector->StartRun();
		}
	}
}

void AFPSRGameMode::EndRun(EFPSRRunOutcome Outcome)
{
	if (!HasAuthority() || bRunEnded)
	{
		return;
	}

	bRunEnded = true;

	// Notify each player with the run outcome (they'll show the result widget locally).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
		{
			PC->ClientShowRunResult(Outcome);
		}
	}

	// TODO(P6): optional GameState freeze once merge-safe.
}

#if !UE_BUILD_SHIPPING

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCmd_EndRun(
		TEXT("FPSR.EndRun"),
		TEXT("End the run with a specified outcome (debug, authority/host only). Usage: FPSR.EndRun [victory|defeat]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AFPSRGameMode* GM = World ? World->GetAuthGameMode<AFPSRGameMode>() : nullptr;
			if (!GM)
			{
				return;
			}

			EFPSRRunOutcome Outcome = EFPSRRunOutcome::Victory;
			if (Args.Num() > 0 && Args[0].Equals(TEXT("defeat"), ESearchCase::IgnoreCase))
			{
				Outcome = EFPSRRunOutcome::Defeat;
			}

			GM->EndRun(Outcome);
		}));

	FAutoConsoleCommandWithWorld GCmd_ReturnToMenu(
		TEXT("FPSR.ReturnToMenu"),
		TEXT("Return to the main menu (debug, host only)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World || (!World->IsNetMode(NM_ListenServer) && !World->IsNetMode(NM_Standalone)))
			{
				return;
			}

			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UFPSRGameFlowSubsystem* Flow = GI->GetSubsystem<UFPSRGameFlowSubsystem>())
				{
					Flow->ReturnToMenu(EFPSRRunOutcome::None);
				}
			}
		}));
}

#endif // !UE_BUILD_SHIPPING

