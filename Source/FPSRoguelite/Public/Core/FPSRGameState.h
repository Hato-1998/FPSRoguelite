// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameStateBase.h"
#include "FPSRGameState.generated.h"

/** Base game state. Run management subsystems are added in later phases. */
UCLASS()
class FPSROGUELITE_API AFPSRGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFPSRGameState();
};
