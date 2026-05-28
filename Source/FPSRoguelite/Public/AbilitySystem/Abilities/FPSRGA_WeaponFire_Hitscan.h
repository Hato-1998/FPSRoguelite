// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRGA_WeaponFire_Hitscan.generated.h"

/** Hitscan fire ability: traces from the player view, draws a debug line, applies damage to enemies. */
UCLASS()
class FPSROGUELITE_API UFPSRGA_WeaponFire_Hitscan : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGA_WeaponFire_Hitscan();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
