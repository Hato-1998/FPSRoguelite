// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameMode.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Hero/FPSRCharacter.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Engine/World.h"

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
