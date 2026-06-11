// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRMenuGameMode.h"
#include "Core/FPSRMenuPlayerController.h"
#include "GameFramework/SpectatorPawn.h"

AFPSRMenuGameMode::AFPSRMenuGameMode()
{
	PlayerControllerClass = AFPSRMenuPlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}
