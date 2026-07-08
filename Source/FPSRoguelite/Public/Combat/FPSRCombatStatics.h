// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"

class AActor;
class UWorld;
class UPrimitiveComponent;

/**
 * Server-authoritative combat resolution helpers (P5 friendly fire / self-damage / explosions / knockback).
 *
 * First principle: instead of scattering `if (EnemyHealthComponent) ApplyDamage` across every weapon path, all
 * damage flows through one place that answers "should this hit land, and for how much?" (enemy / self / friendly
 * + FF scale) and one place that bridges the final amount to the right receiver (enemy health component vs player
 * GAS). Explosions and knockback compose on top. Every entry point assumes the CALLER has already verified server
 * authority — these run only on the server.
 *
 * These helpers assume the INSTIGATOR is a player (player weapons). Enemy-instigated damage (B1 ranged enemies,
 * boss explosions) keeps its existing team-specific path for now; ApplyExplosion's knockback is generic enough to
 * be reused by enemy explosions, but its damage resolution is player-instigator only (a follow-up adds a team arg).
 */
namespace FPSRCombat
{
	/** Outcome of an ApplyDamage call. Three independent axes so a corpse re-hit / overkill is unambiguous:
	 *   - bApplied  : a damage receiver was found and ApplyDamage was invoked. Drives penetration / impact GEOMETRY.
	 *                 Stays true even on a corpse re-hit (a bullet still passes "through" the body, spends pierce).
	 *   - bKilled   : this hit transitioned the enemy ALIVE -> DEAD (bJustKilled). A corpse re-hit reports false,
	 *                 so kill-markers / knockback-exclusion / kill aggregates never double-fire on a corpse.
	 *   - DamageDealt: actual health removed (clamped; 0 on a corpse re-hit, and the overkill excess is excluded).
	 *                 Drives hit-markers, the DealtDamage GAS event, and lifesteal — the "real damage" axis. */
	struct FDamageResult
	{
		bool bApplied = false;
		bool bKilled = false;
		bool bWasEnemy = false;
		float DamageDealt = 0.0f;
	};

	/** Enemies an explosion freshly killed (alive->dead this blast). Inline-sized (<=8) to avoid a heap alloc on the
	 *  per-explosion path; >8 kills falls back to one allocation (rare). Consumed by the weapon OnKill bridge. */
	using FKilledEnemies = TArray<AActor*, TInlineAllocator<8>>;

	/** Outcome of an ApplyExplosion call for the weapon behavior hooks: which enemies it freshly killed (OnKill) and
	 *  whether it dealt real damage to ANY enemy (so a splash that connects doesn't count the activation as a miss —
	 *  e.g. ExplosiveRounds + AmmoOnMiss on the same weapon must not refund ammo on a successful wall-splash hit). */
	struct FExplosionResult
	{
		FKilledEnemies KilledEnemies;
		bool bAnyEnemyHit = false;
	};

	/** One actor's collapsed contribution from a multi-hit trace: the NEAREST hit's distance/impact (for wall
	 *  cutoff + penetration ordering) plus the HIGHEST weakpoint multiplier among that actor's hits. */
	struct FResolvedHit
	{
		AActor* Actor = nullptr;
		float Distance = 0.0f;
		FVector ImpactPoint = FVector::ZeroVector;
		float WeakpointMultiplier = 1.0f;
	};

	/** The map an actor belongs to (multimap Tier 0): a swarm enemy -> its MapId; a player pawn -> its PlayerState's
	 *  committed CurrentMapId; anything else (doors, world, projectiles) -> unset. Unset = the Default single-map. */
	FPSROGUELITE_API FGameplayTag GetActorMapId(const AActor* Actor);

	/** Combat reachability guard. U (P-C): when a unified continuous field is active, false when the ORIGIN cell and the
	 *  TARGET cell are NOT in the same open-grid connected component — a closed door/wall blocks damage/AOE, an open door
	 *  connects them (fail-closed off-grid / during the one-tick post-door-stamp window). OriginLocation is the blast Center
	 *  for explosions, the instigator's location for direct shots. Fallback (no unified grid: single-map / pre-content): the
	 *  MapId cross-map gate (false only when both are settled in DIFFERENT maps). Also covers explosion knockback (0-damage).
	 *  O(1): a precomputed component-label compare, off the swarm hot path. */
	FPSROGUELITE_API bool CanAffectTarget(const UWorld* World, const AActor* Instigator, const AActor* Target, const FVector& OriginLocation);

	/** Host friendly-fire toggle from the run's GameState (false if unavailable). */
	FPSROGUELITE_API bool IsFriendlyFireEnabled(const UWorld* World);

	/** Friendly-player damage multiplier from the GameState (default 0.5 if unavailable). */
	FPSROGUELITE_API float GetFriendlyFireScale(const UWorld* World);

	/** Add BOTH damageable pawn object types — enemies (ECC_Pawn) and players (ECC_FPSRPlayerPawn) — to an object
	 *  query, so a single overlap/trace finds every potential friendly-fire target. */
	FPSROGUELITE_API void AddDamageablePawnObjectTypes(FCollisionObjectQueryParams& OutParams);

	/** Resolve the damage a player Instigator should deal to Target (0 = skip):
	 *   - Target == Instigator   -> bAllowSelf ? BaseDamage : 0   (explosions self-damage; direct shots never do)
	 *   - Target is a swarm enemy -> BaseDamage                    (always full)
	 *   - Target is another player -> FF on ? BaseDamage * scale : 0
	 *   - anything else           -> 0
	 *  Also runs the CanAffectTarget reachability gate. OriginOverride = the connectivity origin: nullptr (default) uses the
	 *  instigator's location (direct shots); explosions pass the blast Center so a wall-splashed target gates on the blast,
	 *  not the shooter's position.
	 */
	FPSROGUELITE_API float ResolveDamage(const AActor* Instigator, const AActor* Target, float BaseDamage,
		bool bAllowSelf, const UWorld* World, const FVector* OriginOverride = nullptr);

	/** Bridge FinalDamage to the receiver that matches Target's kind (enemy health component vs player GAS) and
	 *  report what happened. No-op (bApplied=false) for FinalDamage <= 0 or an unrecognized target. */
	FPSROGUELITE_API FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator, FGameplayTag DamageType = FGameplayTag());

	/** Server: notify the instigating player's controller of a hit-marker (one strongest-outcome pulse). No-op for
	 *  a non-player instigator. Mirrors the per-path aggregation: Kill > Crit > Hit (Game.MD §2-14). */
	FPSROGUELITE_API void NotifyHitMarker(const AActor* Instigator, bool bCrit, bool bKill);

	/** Radial explosion: overlap every damageable pawn in range, apply ResolveDamage/ApplyDamage with a per-target
	 *  crit roll, fire ONE hit-marker if any enemy was hit, and apply knockback (independent of damage — see below).
	 *  bAllowSelf gates instigator self-damage. Does NOT ignore the instigator (so self-damage/self-knockback work).
	 *
	 *  Knockback (KnockbackStrength > 0): a radial impulse pushing every survivor outward from Center, magnitude
	 *  falling off linearly to the rim. Applied EVEN when damage is 0 (FF-off friendly / self-no-damage) — only the
	 *  freshly killed are excluded. Player knockback launches the character (rocket jump / ally launch). */
	FPSROGUELITE_API FExplosionResult ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength, FGameplayTag DamageType = FGameplayTag());

	/** Dispatch a knockback velocity to Target: players -> additive LaunchCharacter (preserves jump for rocket
	 *  jumping); swarm enemies -> AFPSREnemyBase decaying-velocity knockback (integrated by their movement tick). */
	FPSROGUELITE_API void ApplyKnockback(AActor* Target, const FVector& Velocity);

	/** Add the weakpoint object type to an object query (line-trace damage paths only — NOT the explosion query,
	 *  which must never gather weakpoints). */
	FPSROGUELITE_API void AddWeakpointObjectType(FCollisionObjectQueryParams& OutParams);

	/** Weakpoint damage multiplier for a hit primitive (1.0 if it is not a UFPSRWeakpointComponent). */
	FPSROGUELITE_API float GetWeakpointMultiplier(const UPrimitiveComponent* Component);

	/** Highest weakpoint multiplier among Target's UFPSRWeakpointComponents whose sphere intersects the query
	 *  sphere (SphereCenter/SphereRadius). 1.0 if none — used by the sphere-overlap paths (projectile / melee)
	 *  so a body-first overlap still upgrades to a weakpoint hit (re-queried at damage time, not event order). */
	FPSROGUELITE_API float GetBestWeakpointMultiplierForSphere(const AActor* Target, const FVector& SphereCenter, float SphereRadius);

	/** Collapse a distance-sorted multi-hit result to ONE entry per actor (nearest distance/impact kept; weakpoint
	 *  multiplier = max across that actor's hits), preserving nearest-first order. Shared by hitscan + charge-laser
	 *  so the same actor's body + weakpoint hits never double-damage or double-spend penetration. */
	FPSROGUELITE_API void DedupePawnHitsByActor(const TArray<FHitResult>& InHits, TArray<FResolvedHit>& OutHits);
}
