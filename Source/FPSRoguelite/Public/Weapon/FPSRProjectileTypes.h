// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/FPSRVitals.h"
#include "Combat/FPSRCritTypes.h"
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

	/** CRIT1: the full crit rule, baked from the instigator's ASC (+ weapon fragments/timed buffs) at spawn and
	 *  rolled per impact. Default {Chance 0, Multiplier 1} = never crit (enemy-fired projectiles leave this at
	 *  default; only player weapons carry crit). Replaces the former separate CritChance/CritMultiplier floats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	FFPSRCritContext Crit;

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

	/** VIT1: the anti-shield multiplier (and DamageType) baked from the weapon's resolved stats AT LAUNCH. A
	 *  projectile is separated from its shooter in time and space — re-reading weapon state at IMPACT would pick up
	 *  a weapon swap or a card eaten mid-flight, exactly the reason Pierce/GravityScale already travel this way
	 *  instead of being re-resolved on arrival (VIT1 §5-9). Not a UPROPERTY (like the rest of this struct's plain
	 *  members it needs no editor exposure), and safe to leave out of replication for the same reason WeaponInstance
	 *  is: FFPSRProjectileParams is server-only state, never listed in GetLifetimeReplicatedProps. */
	FFPSRDamageSpec DamageSpec;
};
