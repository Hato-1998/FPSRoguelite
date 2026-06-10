// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRGA_WeaponFire_Projectile.generated.h"

/** Projectile fire ability (AOE archetypes: bazooka/grenade): spawns server-authoritative pooled projectiles
 *  from the projectile subsystem. Reuses the freeze / fire-rate / ammo gates and the weapon behavior fragments. */
UCLASS()
class FPSROGUELITE_API UFPSRGA_WeaponFire_Projectile : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGA_WeaponFire_Projectile();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
