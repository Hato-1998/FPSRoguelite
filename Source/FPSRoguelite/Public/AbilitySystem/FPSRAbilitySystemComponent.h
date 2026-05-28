// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "FPSRAbilitySystemComponent.generated.h"

/** Project AbilitySystemComponent. Lives on the PlayerState. */
UCLASS()
class FPSROGUELITE_API UFPSRAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UFPSRAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
