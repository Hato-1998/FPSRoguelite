// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "FPSRGameplayAbility.generated.h"

/** Project base GameplayAbility. Common helpers/policies live here. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGameplayAbility();
};
