// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameModeBase.h"
#include "FPSRGameMode.generated.h"

/** Default game mode wiring the project's GameState, PlayerController, PlayerState and Character. */
UCLASS()
class FPSROGUELITE_API AFPSRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRGameMode();
};
