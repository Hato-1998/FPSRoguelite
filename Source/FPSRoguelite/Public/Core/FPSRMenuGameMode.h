// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPSRMenuGameMode.generated.h"

class AFPSRMenuPlayerController;

/** Menu-only game mode: sets the menu player controller and spectator pawn.
 *  No director, no enemies, no cards — just UI. */
UCLASS()
class FPSROGUELITE_API AFPSRMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRMenuGameMode();
};
