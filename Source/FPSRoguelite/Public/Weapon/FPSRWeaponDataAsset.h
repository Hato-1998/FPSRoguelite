// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponDataAsset.generated.h"

class UGameplayAbility;
class UFPSRCardDataAsset;
class AFPSRProjectile;
class USkeletalMesh;
class UStaticMesh;
class UAnimInstance;
class UAnimMontage;
class USoundBase;
class UParticleSystem;

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

	// --- 1P visual / cosmetic (Game.MD §2-9, V0) — all soft refs, null = no visual (no gameplay effect) ---

	/** First-person weapon skeletal mesh (firearms). Attached to the character's FirstPersonArms on equip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh1P;

	/** First-person weapon static mesh (e.g. melee knife). Used when WeaponMesh1P is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UStaticMesh> WeaponMeshStatic1P;

	/** Optional per-weapon arms anim instance applied to FirstPersonArms on equip (the pack has per-weapon arm anims). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftClassPtr<UAnimInstance> ArmsAnimInstanceClass;

	/** Socket on FirstPersonArms the weapon mesh attaches to (NAME_None = arms component root). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName WeaponAttachSocket = NAME_None;

	/** Socket on the WEAPON mesh used as the muzzle-flash origin (cosmetic only; trace origin stays the camera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName MuzzleSocket = NAME_None;

	/** Optional montage played on the arms when this weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> EquipMontage;

	/** Optional montage played on the arms each shot (owner-client local feel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> FireMontage;

	/** Cascade muzzle-flash particle spawned at MuzzleSocket each shot (owner-client local). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	/** Fire sound played each shot (owner-client local; multi-client cosmetic is a later unit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Audio")
	TSoftObjectPtr<USoundBase> FireSound;

#if WITH_EDITOR
	/** Editor validation: missing FireAbility never fires (error); archetype/stat mismatches (AOE without an
	 *  AOERadius, ChargeLaser with ChargeTime 0, ranged with MagSize 0) silently misbehave at runtime (warn). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
