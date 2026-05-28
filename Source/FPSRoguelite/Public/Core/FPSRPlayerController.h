// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "FPSRPlayerController.generated.h"

/** Base player controller. EnhancedInput binding is added in a later P1 step. */
UCLASS()
class FPSROGUELITE_API AFPSRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRPlayerController();
};
