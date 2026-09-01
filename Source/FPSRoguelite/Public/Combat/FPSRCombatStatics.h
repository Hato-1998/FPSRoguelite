// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "Combat/FPSRVitals.h"
#include "Hero/FPSRFeedbackTypes.h"

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
	 *                 Drives hit-markers, the DealtDamage GAS event, and lifesteal — the "real damage" axis.
	 *                 🔴 VIT1 redefinition — this is now `ShieldSpent + HealthSpent` (FPSRVitals::FResult::TotalSpent),
	 *                 not `HealthBefore - HealthAfter`: a hit a shield fully absorbs still counts as real damage
	 *                 (hit-markers / lifesteal / penetration checks must not go silent just because health didn't move).
	 *   - bShieldBroke: this hit took the target's shield from >0 to 0 (VIT1 requirement 6 — propagates to the
	 *                 attacker's hit-marker and, via replication, to the target's own cosmetics). */
	struct FDamageResult
	{
		bool bApplied = false;
		bool bKilled = false;
		bool bWasEnemy = false;
		float DamageDealt = 0.0f;
		bool bShieldBroke = false;
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

	/** Combat reachability guard. U (P-C): when a MULTI-SLOT unified field is active, false when the ORIGIN cell and the
	 *  TARGET cell are NOT in the same open-grid connected component — a closed door/wall blocks damage/AOE, an open door
	 *  connects them (fail-closed off-grid / during the one-tick post-door-stamp window). OriginLocation is the blast Center
	 *  for explosions, the instigator's location for direct shots. P-G: single-map degenerate grid / no field -> allow-all
	 *  (no cross-map walls to gate; matches pre-P-G single-map). Also covers explosion knockback (0-damage).
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
	 *  report what happened. No-op (bApplied=false) for FinalDamage <= 0 or an unrecognized target.
	 *  🔴 VIT1 signature change: the trailing `FGameplayTag DamageType` U18a added is replaced by `FFPSRDamageSpec`
	 *  (adds the anti-shield multiplier alongside the damage type) — the default value preserves every existing
	 *  call site's behavior (empty DamageType = Physical, ShieldDamageMultiplier = 1.0). */
	FPSROGUELITE_API FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator, const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

	/** Server: notify the instigating player's controller of a hit-marker (one strongest-outcome pulse). No-op for
	 *  a non-player instigator. Mirrors the per-path aggregation, now via ResolveHitMarker: Kill > ShieldBreak > Crit
	 *  > Hit (Game.MD §2-14; no bWeak here — an explosion has no single targeted weakpoint). */
	FPSROGUELITE_API void NotifyHitMarker(const AActor* Instigator, bool bCrit, bool bKill, bool bShieldBreak = false);

	/** VIT1: the single owner of hit-marker PRIORITY, shared by all 5 damage paths (Hitscan / ChargeLaser / Melee /
	 *  Projectile / NotifyHitMarker above) so adding ShieldBreak meant editing this once instead of the same 3-way
	 *  ternary in 5 places (the exact "5-path scatter" CombatWeaponCard.md §2-3-5 warns about). Pure function.
	 *  Priority: Kill > ShieldBreak > Weak > Crit > Hit. */
	FPSROGUELITE_API EFPSRHitMarkerType ResolveHitMarker(bool bKill, bool bShieldBreak, bool bWeak, bool bCrit);

	/** Radial explosion: overlap every damageable pawn in range, apply ResolveDamage/ApplyDamage with a per-target
	 *  crit roll, fire ONE hit-marker if any enemy was hit, and apply knockback (independent of damage — see below).
	 *  bAllowSelf gates instigator self-damage. Does NOT ignore the instigator (so self-damage/self-knockback work).
	 *
	 *  Knockback (KnockbackStrength > 0): a radial impulse pushing every survivor outward from Center, magnitude
	 *  falling off linearly to the rim. Applied EVEN when damage is 0 (FF-off friendly / self-no-damage) — only the
	 *  freshly killed are excluded. Player knockback launches the character (rocket jump / ally launch). */
	FPSROGUELITE_API FExplosionResult ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength, const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

	/** Dispatch a knockback velocity to Target: players -> additive LaunchCharacter (preserves jump for rocket
	 *  jumping); swarm enemies -> AFPSREnemyBase decaying-velocity knockback (integrated by their movement tick). */
	FPSROGUELITE_API void ApplyKnockback(AActor* Target, const FVector& Velocity);

	/** Add the weakpoint object type to an object query (line-trace damage paths only — NOT the explosion query,
	 *  which must never gather weakpoints). */
	FPSROGUELITE_API void AddWeakpointObjectType(FCollisionObjectQueryParams& OutParams);

	/** Add the destructible object type (doors / arena props) to an object query. EVERY damage path that can reach
	 *  breakable geometry must call this alongside AddDamageablePawnObjectTypes — a destructible is not a pawn, so
	 *  the pawn types alone no longer find it. Kept separate rather than folded into AddDamageablePawnObjectTypes so
	 *  each query still states exactly what it gathers (same reason AddWeakpointObjectType is its own call). */
	FPSROGUELITE_API void AddDestructibleObjectType(FCollisionObjectQueryParams& OutParams);

	/** True when a primitive gathered by a damage object-query is breakable GEOMETRY (door leaf / arena prop)
	 *  rather than a pawn. The line-trace paths must NOT put these in their wall-trace ignore list: a destructible
	 *  IS the wall, so everything behind it stays out of range. A pawn is the opposite case — it must never
	 *  masquerade as cover — which is why the ignore list exists at all. */
	FPSROGUELITE_API bool IsDestructibleGeometry(const UPrimitiveComponent* Component);

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
