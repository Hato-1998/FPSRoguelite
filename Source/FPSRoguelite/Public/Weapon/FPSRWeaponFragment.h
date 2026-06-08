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

	/** Hook surface (default no-ops). Only the hitscan-relevant hooks are defined here; charge/projectile
	 *  hooks (ModifyChargeTime/OnProjectileSpawn) are added when those archetypes arrive. */
	virtual void PreFire(FFPSRFireContext& Context) const {}
	virtual void ModifyShotCount(FFPSRFireContext& Context) const {}
	virtual void OnHitActor(const FFPSRFireContext& Context, AActor* HitActor, float& DamageInOut) const {}
	virtual void PostFire(const FFPSRFireContext& Context) const {}
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
