// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRGA_WeaponFire_ChargeLaser.generated.h"

/** ChargeLaser fire ability: hold-to-charge, release-to-fire piercing hitscan beam (Game.MD §2-10 laser = hitscan).
 *  The charge alpha — measured server-authoritatively by the weapon fire component — scales damage. The beam
 *  pierces every enemy up to the first world blocker. Activated on trigger release by the fire component. */
UCLASS()
class FPSROGUELITE_API UFPSRGA_WeaponFire_ChargeLaser : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGA_WeaponFire_ChargeLaser();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
