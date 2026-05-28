// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRGA_WeaponMelee.generated.h"

/** Melee ability: sphere overlap in front of the player, applies damage to all enemies hit. */
UCLASS()
class FPSROGUELITE_API UFPSRGA_WeaponMelee : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGA_WeaponMelee();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
