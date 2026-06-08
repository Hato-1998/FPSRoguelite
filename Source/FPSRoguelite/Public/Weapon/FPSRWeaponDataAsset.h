// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponDataAsset.generated.h"

class UGameplayAbility;
class UFPSRCardDataAsset;

/** Data-driven weapon definition. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	EFPSRWeaponArchetype Archetype = EFPSRWeaponArchetype::FullAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FFPSRWeaponStatBlock BaseStats;

	/** Ability granted while this weapon is equipped (activated by the Fire input). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<UGameplayAbility> FireAbility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponCards;

	/** Behavior-fragment reward cards (Scope=ThisWeapon, GrantedFragment set) offerable as this weapon's
	 *  mission-clear reward (Game.MD §2-4-1 ②). One is chosen at the mission-reward freeze. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> AvailableModifiers;
};
