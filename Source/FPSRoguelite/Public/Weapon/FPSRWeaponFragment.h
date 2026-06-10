// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRWeaponFragment.generated.h"

class APawn;
class AActor;
class AController;
class UWorld;
class UFPSRWeaponInstance;
struct FFPSRProjectileParams;

/**
 * Transient per-activation firing context passed to weapon behavior-fragment hooks. Plain struct (not a
 * USTRUCT) — never replicated or reflected, lives only on the stack during a single fire ability activation,
 * so hooks stay allocation-free.
 */
struct FFPSRFireContext
{
	APawn* Avatar = nullptr;
	AController* Controller = nullptr;
	UWorld* World = nullptr;
	UFPSRWeaponInstance* Instance = nullptr;

	/** Number of shots/traces this activation. ModifyShotCount adjusts it (clamped by the ability). */
	int32 ShotCount = 1;

	/** True on the server (authority) — hooks that mutate game state must gate on this. */
	bool bAuthority = false;

	/** Set by the NoSelfDamage card (PreFire): suppress instigator self-damage on this activation's explosions.
	 *  Knockback stays on (rocket jump without self-harm). Baked into projectile bSelfDamage / hitscan-AOE bAllowSelf. */
	bool bSuppressSelfDamage = false;
};

/**
 * Weapon behavior fragment (Game.MD §2-4-1 ②): a data-driven, stateless modifier that changes how a weapon
 * fires via composable hooks (multishot, pierce, healing beam, …). The behavior is a C++ subclass; the tuning
 * values live on the authored DataAsset instance. A UFPSRWeaponInstance accumulates references to these shared
 * assets (no per-instance state) — hooks run once per fire (Pre/ModifyShotCount/Post) or per hit (OnHitActor,
 * a handful per shot), so virtual dispatch stays cheap and hits never allocate.
 */
UCLASS(Abstract, BlueprintType)
class FPSROGUELITE_API UFPSRWeaponFragment : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	FText DisplayName;

	/** Optional identity tag (dedup / future "swap" logic). Identity for HasFragment is the asset pointer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	FGameplayTag FragmentTag;

	/** How many times this fragment can be stacked on one weapon. Each stack re-applies the hooks (e.g.
	 *  MultiShot's ExtraShots adds per stack), and the mission-reward offer keeps presenting the card until
	 *  the weapon holds MaxStacks copies. 1 = single pick (default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** Hook surface (default no-ops). OnProjectileSpawn mutates AOE projectile params; ModifyChargeTime adjusts
	 *  the ChargeLaser charge-up duration. */
	virtual void PreFire(FFPSRFireContext& Context) const {}
	virtual void ModifyShotCount(FFPSRFireContext& Context) const {}
	virtual void OnHitActor(const FFPSRFireContext& Context, AActor* HitActor, float& DamageInOut) const {}
	virtual void PostFire(const FFPSRFireContext& Context) const {}

	/** Projectile-spawn hook (AOE archetypes): mutate the projectile spawn params before it is acquired. */
	virtual void OnProjectileSpawn(const FFPSRFireContext& Context, FFPSRProjectileParams& ParamsInOut) const {}

	/** Charge-time hook (ChargeLaser): adjust the seconds-to-full-charge before the charge alpha is computed. */
	virtual void ModifyChargeTime(const FFPSRFireContext& Context, float& ChargeTimeInOut) const {}

	/** Hitscan impact hook (server-only): called at each terminal impact point of a hitscan pellet so a fragment
	 *  can spawn an effect at the hit — e.g. ExplosiveRounds turns a rifle hit into a small AOE. bAllowSelf passes
	 *  through the NoSelfDamage suppression so a spawned explosion respects it. */
	virtual void OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf) const {}
};

/** Reference fragment: fires extra shots/pellets per activation (e.g. 2-round multishot, shotgun spread). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_MultiShot : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "1"))
	int32 ExtraShots = 1;

	virtual void ModifyShotCount(FFPSRFireContext& Context) const override
	{
		Context.ShotCount += ExtraShots;
	}
};

/** Reference fragment: flat bonus damage applied per hit (exercises the per-hit OnHitActor hook). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_OnHitBonusDamage : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	float BonusDamage = 10.0f;

	virtual void OnHitActor(const FFPSRFireContext& Context, AActor* HitActor, float& DamageInOut) const override
	{
		DamageInOut += BonusDamage;
	}
};

/** Card A — NoSelfDamage: suppress the instigator's self-damage from explosions for this weapon (the auto-damage
 *  of self-fired AOE / explosive rounds is nullified). Knockback is unaffected, so rocket-jumping still works. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_NoSelfDamage : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	virtual void PreFire(FFPSRFireContext& Context) const override
	{
		Context.bSuppressSelfDamage = true;
	}
};

/** Card B — ExplosiveRounds: turns each hitscan impact into a small radial explosion (rifle → splash). Damage and
 *  knockback follow the same self/friendly rules as any explosion (the spawned blast is server-authoritative). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_ExplosiveRounds : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	/** Radius of the per-impact explosion (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float AOERadius = 150.0f;

	/** Damage dealt at the center of each per-impact explosion (before self/friendly resolution). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float AOEDamage = 20.0f;

	/** Radial knockback impulse of the per-impact explosion (cm/s); 0 = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float KnockbackStrength = 0.0f;

	virtual void OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf) const override;
};
