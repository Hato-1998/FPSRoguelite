// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponDataAsset.generated.h"

class UGameplayAbility;
class UFPSRCardDataAsset;
class AFPSRProjectile;

/** Data-driven weapon definition. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	/** Weapon archetype now lives in BaseStats so per-archetype stat fields can drive EditCondition visibility. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EFPSRWeaponArchetype GetArchetype() const { return BaseStats.Archetype; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FFPSRWeaponStatBlock BaseStats;

	/** Ability granted while this weapon is equipped (activated by the Fire input). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<UGameplayAbility> FireAbility;

	/** Projectile actor class (AOE archetypes). Content assigns a BP with mesh/VFX; null falls back to AFPSRProjectile base. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	TSubclassOf<AFPSRProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponCards;

	/** Behavior-fragment reward cards (Scope=ThisWeapon, GrantedFragment set) offerable as this weapon's
	 *  mission-clear reward (Game.MD §2-4-1 ②). One is chosen at the mission-reward freeze. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> AvailableModifiers;

	/** Stat axes this weapon OPTS OUT of for AllWeapons-scope modifier cards (Game.MD §2-4-1 ①). A broad
	 *  "all weapons" card on a listed axis is skipped when this weapon resolves its stats — e.g. a ChargeLaser whose
	 *  recoil is a charge ramp can list RecoilVertical so a global "recoil down" card doesn't touch it. Per-weapon,
	 *  per-axis, AllWeapons-only: ThisWeapon cards (the player deliberately targeted this weapon) always apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<EFPSRWeaponStat> AllWeaponsStatExclusions;

#if WITH_EDITOR
	/** Editor validation: missing FireAbility never fires (error); archetype/stat mismatches (AOE without an
	 *  AOERadius, ChargeLaser with ChargeTime 0, ranged with MagSize 0) silently misbehave at runtime (warn). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
