// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AActor;
class UWorld;

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
	/** Outcome of an ApplyDamage call, used for hit-marker feedback and knockback (exclude the freshly killed). */
	struct FDamageResult
	{
		bool bApplied = false;  // a damage receiver was found and damage was applied
		bool bKilled = false;   // the target died from this hit (enemies only; player DBNO is a later phase)
		bool bWasEnemy = false; // the target was a swarm enemy (drives the firing player's hit-marker)
	};

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
	 */
	FPSROGUELITE_API float ResolveDamage(const AActor* Instigator, const AActor* Target, float BaseDamage,
		bool bAllowSelf, const UWorld* World);

	/** Bridge FinalDamage to the receiver that matches Target's kind (enemy health component vs player GAS) and
	 *  report what happened. No-op (bApplied=false) for FinalDamage <= 0 or an unrecognized target. */
	FPSROGUELITE_API FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator);

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
	FPSROGUELITE_API void ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength);

	/** Dispatch a knockback velocity to Target: players -> additive LaunchCharacter (preserves jump for rocket
	 *  jumping); swarm enemies -> AFPSREnemyBase decaying-velocity knockback (integrated by their movement tick). */
	FPSROGUELITE_API void ApplyKnockback(AActor* Target, const FVector& Velocity);
}
