// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameMode.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Hero/FPSRCharacter.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardPoolDataAsset.h"
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
	}
}
