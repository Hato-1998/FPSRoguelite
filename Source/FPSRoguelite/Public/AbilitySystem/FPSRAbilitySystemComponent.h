// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "FPSRAbilitySystemComponent.generated.h"

/** Project AbilitySystemComponent. Two owners with DIFFERENT patterns (do not assume PlayerState):
 *  - Players: owned by AFPSRPlayerState (survives respawn/seamless travel), replication mode Mixed.
 *  - Elite-tier enemies: owned by the ACTOR itself (AFPSREnemyEliteBase — enemies have no PlayerState),
 *    replication mode Minimal (no owning client at all), and bound by the elite time-axis contract
 *    (no duration/periodic/cooldown GEs — see Docs/SSOT/Enemy.md §2-6). ADR 0013. */
UCLASS()
class FPSROGUELITE_API UFPSRAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UFPSRAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
