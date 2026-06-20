// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRProjectileTypes.generated.h"

class UFPSRWeaponInstance;

UENUM(BlueprintType)
enum class EFPSRProjectileTeam : uint8
{
	Player,
	Enemy
};

USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRProjectileParams
{
	GENERATED_BODY()

	/** Initial projectile velocity (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float InitialSpeed = 3000.0f;

	/** Gravity scale. 0 = straight (rocket); >0 = arc (grenade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float GravityScale = 0.0f;

	/** Damage per hit or per target in AOE (global damage multiplier already baked at spawn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float Damage = 50.0f;

	/** Crit chance [0,1], baked from the instigator's ASC at spawn and rolled per impact. 0 = never crit
	 *  (enemy-fired projectiles leave this 0; only player weapons carry crit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float CritChance = 0.0f;

	/** Damage multiplier applied on a successful crit roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float CritMultiplier = 1.0f;

	/** Lifetime before auto-release (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float Lifetime = 5.0f;

	/** AOE explosion radius (cm). >0 = radial damage on impact; 0 = single-target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float ExplosionRadius = 0.0f;

	/** Extra pawns a single-hit projectile passes through before stopping (ignored if AOE). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	int32 Pierce = 0;

	/** AOE only: whether the explosion damages the instigator (self/auto-damage). The NoSelfDamage card clears
	 *  this. Knockback is independent — a self-no-damage explosion still launches the instigator (rocket jump). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	bool bSelfDamage = true;

	/** AOE only: radial knockback impulse magnitude (cm/s) applied to survivors in range. 0 = no knockback.
	 *  Independent of damage (applies even at 0 friendly/self damage); the freshly-killed are excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float KnockbackStrength = 0.0f;

	/** Team affiliation (determines damage targets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	EFPSRProjectileTeam Team = EFPSRProjectileTeam::Player;

	/** The actor that fired this projectile (never damaged by its own projectile). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	/** SERVER-ONLY back-reference to the weapon instance that fired this projectile, for the U18c behavior-trigger
	 *  bridge (OnKill) at damage time. Weak so it auto-nulls if the player swaps/drops the weapon mid-flight — the
	 *  hook then degrades gracefully (no fragments). NOT replicated: FFPSRProjectileParams is server-side state and
	 *  is never registered in GetLifetimeReplicatedProps, so this handle never crosses the wire. If Params is ever
	 *  made replicated, this field MUST be excluded. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFPSRWeaponInstance> WeaponInstance = nullptr;

	/** SERVER-ONLY (U18c): true if this projectile was the ONLY one spawned by its activation. The OnMiss behavior
	 *  hook is per-ACTIVATION on every other fire path, but projectiles release asynchronously and independently —
	 *  so it only fires the miss hook for single-projectile activations (where per-projectile == per-activation:
	 *  sniper/bazooka/grenade). A multishot projectile volley leaves this false and fires no per-projectile OnMiss,
	 *  avoiding over-/partial-refunds (AmmoOnMiss + MultiShot on a projectile weapon is an unsupported rare combo). */
	UPROPERTY(Transient)
	bool bSingleProjectileActivation = true;
};
