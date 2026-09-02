// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/FPSRVitals.h"   // FFPSRDamageSpec (server-only payload below)
#include "FPSRBossTypes.generated.h"

/** One pending delayed blast (BOSS1 barrage). The boss owns these as a REPLICATED ARRAY rather than spawning one
 *  actor per marker:
 *   ① The default NetCullDistance (150 m) is smaller than the arena diagonal (226 m for the 160x160 m arena,
 *      ADR 0011 E1) — actor markers would be invisible to distant teammates, and a marker has to be visible to
 *      everyone precisely so people can run out of it.
 *   ② Zero actor spawn/destroy and zero extra replicated channels (the boss is already bAlwaysRelevant).
 *   ③ One owner for the freeze edge and for teardown (the boss) instead of N self-polling actors.
 *   ④ Join-in-progress is free: the array IS the state.
 *
 *  🔴 Clients read these in IMMEDIATE MODE — a Blueprint walks GetBlastMarks() every frame to draw, and decides
 *  "this one detonated" from DetonateAtClock vs the pattern clock, NOT from the entry disappearing. That is why
 *  there is no id/diff/removal-reason field: a fizzled marker (FIFO eviction, boss death) vanishes BEFORE its
 *  detonation time, so its explosion cosmetic simply never plays. An event-driven design could not tell a fizzle
 *  from a detonation. */
USTRUCT(BlueprintType)
struct FFPSRBossBlastMark
{
	GENERATED_BODY()

	/** Impact point. Z is the TARGET'S LAST GROUNDED Z (UFPSREnemySpawnSubsystem::GetLastGroundedZ), not its current
	 *  Z — a shell aimed at an airborne player must still put its marker on the floor, and that cache answers it with
	 *  zero traces. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Boss")
	FVector_NetQuantize Center = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Boss")
	float Radius = 0.0f;

	/** Detonation time on the BOSS PATTERN CLOCK (freeze-paused), never world time. Clients compare it against
	 *  AFPSRBossBase::GetPatternClockSeconds() so the countdown they draw stops during a level-up freeze too. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Boss")
	float DetonateAtClock = 0.0f;

	/** Who this shell was aimed at — lets a client draw ITS OWN marker differently. With "5 shells per player" and
	 *  4 players, several markers can be up at once and telling them apart is the whole readability question. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Boss")
	TWeakObjectPtr<APawn> TargetPawn;

	// ---- Server-only payload ------------------------------------------------------------------------------------
	// Deliberately NOT UPROPERTY: only the server ever detonates, so replicating the damage numbers would be pure
	// bandwidth. (Contrast with the marker id this struct used to carry — that one WAS needed on clients, and being
	// a non-UPROPERTY silently made it useless there. The test is always "who reads it", not "is it small".)
	// FFPSRDamageSpec is not a USTRUCT at all (VIT1 kept it a plain server-internal value), so it could not be
	// replicated even if we wanted to.

	float Damage = 0.0f;
	float KnockbackStrength = 0.0f;
	FFPSRDamageSpec Spec;
};

/** Pure boss-phase rules. Stateless and replication-free, deliberately — the same shape as namespace FPSRVitals
 *  (VIT1): a pure function costs nothing when nobody calls it, and it can be unit-tested with no world. */
namespace FPSRBoss
{
	/** HealthFraction (0..1) + a DESCENDING threshold array -> 1-based phase. Empty array -> always 1.
	 *  Boundary is inclusive ("at or below"): Fraction <= Thresholds[i] means at least phase i+2.
	 *  The array's LENGTH is what decides how many phases a boss has — there is no phase-count constant in code. */
	FPSROGUELITE_API int32 ComputePhase(float HealthFraction, TConstArrayView<float> Thresholds);

	/** Monotonic latch so healing can never walk a phase back down. */
	FORCEINLINE int32 LatchPhase(int32 Current, int32 Computed) { return FMath::Max(Current, Computed); }
}
