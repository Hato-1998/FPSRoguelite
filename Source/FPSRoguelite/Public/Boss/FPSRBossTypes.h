// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/FPSRVitals.h"   // FFPSRDamageSpec (server-only payload below)
#include "FPSRBossTypes.generated.h"

/** Every boss pattern runs Prep -> Execute -> Recovery. Replicated so a Blueprint can play a wind-up and a
 *  recovery beat; the boss is never mid-pattern without clients knowing which part they are looking at. */
UENUM(BlueprintType)
enum class EFPSRBossPatternStage : uint8
{
	/** Winding up. NOTHING has been spawned yet — this is the system half of "telegraph before you can be hurt". */
	Prep,
	/** The pattern proper. Each pattern's own grace (blast fuse / stationary beam / hovering orbs) lives in here,
	 *  because those three mean different things and folding them into one number would flatten them. */
	Execute,
	/** Idle after the pattern. This is what stops patterns from running back to back. */
	Recovery,
	Finished
};

/** What makes the boss start its NEXT pattern. Patterns are not a queue that drains — the boss idles until one of
 *  these fires (§14-3). */
UENUM(BlueprintType)
enum class EFPSRBossTriggerKind : uint8
{
	/** Seconds since the boss fight began. */
	Elapsed,
	/** Patterns performed so far.
	 *  🔴 This stands in for the "after N basic attacks" the design asks for: the boss has NO basic attack yet, so
	 *  there is nothing to count. When one is added it becomes a new kind here and nothing else changes. */
	PatternCount,
	/** Health fraction dropping to or below the threshold. Fires once.
	 *  Deliberately NOT the same axis as an ability's MinPhase: MinPhase is a GATE ("usable from phase N on"),
	 *  this is a CLOCK ("go now, once"). */
	HealthBelow
};

USTRUCT(BlueprintType)
struct FFPSRBossPatternTrigger
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "FPSR|Boss")
	EFPSRBossTriggerKind Kind = EFPSRBossTriggerKind::Elapsed;

	/** Seconds (Elapsed) / count (PatternCount) / health fraction 0..1 (HealthBelow). */
	UPROPERTY(EditAnywhere, Category = "FPSR|Boss", meta = (ClampMin = "0.0"))
	float Threshold = 6.0f;

	/** Elapsed and PatternCount only: fire every Threshold rather than once. HealthBelow is one-shot by nature. */
	UPROPERTY(EditAnywhere, Category = "FPSR|Boss")
	bool bRepeating = true;

	/** Server-side latch: how many times this trigger has already fired. Not replicated — trigger evaluation is
	 *  server-only, and a client that knew would still have nothing to do with it. */
	int32 FireCount = 0;
};

/** Which pattern to start once a trigger fires. */
UENUM(BlueprintType)
enum class EFPSRBossPatternSelection : uint8
{
	/** Round-robin through GrantedAbilities — predictable, so players can learn the rotation. */
	Sequential,
	/** Uniform among the eligible ones. */
	Random
};

/** Everything a homing orb needs at launch. A struct rather than an eight-argument call, following the same
 *  convention FFPSRServerAttackContext set: new per-launch inputs get added here instead of growing a signature. */
struct FFPSRBossOrbLaunchParams
{
	/** Hover in place around the boss for this long before starting to chase — the pattern's own telegraph, which is
	 *  separate from the boss's system-level wind-up. */
	float GraceSeconds = 0.8f;

	/** How long it steers before giving up and flying straight. */
	float TrackSeconds = 8.0f;

	float Health = 150.0f;

	/** Blast on impact: damages every player in radius, not just whoever it touched. That is what makes clumping
	 *  around the hunted player — or around their body — the wrong answer. */
	float BlastDamage = 20.0f;
	float BlastRadiusCm = 400.0f;
	float BlastKnockback = 0.0f;
};

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

	/** Should this trigger fire right now? Pure, so the cadence of the whole fight is testable with no world.
	 *
	 *  `OutFireCount` is the trigger's new latch value and MUST be written back by the caller when this returns true
	 *  — that latch is the only thing separating "fires once" from "fires every tick from now on".
	 *
	 *  🔴 Elapsed/PatternCount compare against an ACCUMULATED count rather than "time since the last fire". After a
	 *  long level-up freeze the clock has not moved (it is the freeze-paused clock), but if this were written as a
	 *  catch-up loop any drift would come out as a burst of patterns the instant the party unpauses. Counting
	 *  thresholds crossed makes a burst impossible to express. */
	FPSROGUELITE_API bool ShouldTriggerFire(const FFPSRBossPatternTrigger& Trigger, float ElapsedSeconds,
		int32 PatternsPerformed, float HealthFraction, int32& OutFireCount);

	/** What is wrong with an authored trigger, if anything. A pure enum rather than an FText so the rule is testable
	 *  with no editor and no DataValidation context — AFPSRBossBase::IsDataValid is then a thin adapter that only
	 *  chooses the wording. Same shape as ComputePhase/ShouldTriggerFire: the RULE is pure, the plumbing is not. */
	enum class ETriggerAuthoringIssue : uint8
	{
		None,
		/** Elapsed/PatternCount would be due on the very first tick and stay due forever; HealthBelow could only fire
		 *  on the death frame. */
		ThresholdNotPositive,
		/** A HealthBelow trigger at >= 1 fires before the boss has taken a single point of damage. */
		HealthThresholdFull
	};

	FPSROGUELITE_API ETriggerAuthoringIssue ValidateTrigger(const FFPSRBossPatternTrigger& Trigger);

	/** Party size the authoring checks assume. The game is 1-4 players (Game.md §1), and validation has to reason
	 *  about the WORST case or it would pass a setup that only breaks in a full party. */
	inline constexpr int32 MaxSupportedPlayers = 4;

	/** The order the selector visits eligible patterns in.
	 *
	 *  Pulled out as a pure function so the policy is testable without an ASC: Sequential must walk from the cursor
	 *  and wrap, Random must start anywhere but STILL visit every candidate — the second property is the one that is
	 *  easy to lose. A "roll once and give up" random would waste a trigger whenever the rolled pattern happened to
	 *  be on cooldown, and the boss would just stand there for a beat with no way to tell why. */
	FPSROGUELITE_API void BuildSelectionOrder(EFPSRBossPatternSelection Policy, int32 NumEligible, int32 Cursor,
		int32 RandomStart, TArray<int32>& OutOrder);

	/** Peak number of blast markers a barrage can have alive at once: one per player per interval, for as many
	 *  intervals as fit inside a fuse. Used by IsDataValid so a designer hears about an over-cap combination while
	 *  authoring rather than discovering it as silently fizzled shells mid-fight. */
	FPSROGUELITE_API int32 EstimatePeakBlastMarks(int32 MaxPlayers, float FuseSeconds, float IntervalSeconds);

#if !UE_BUILD_SHIPPING
	/** `FPSR.BossDebugDraw 1` — draw the boss's pattern state as debug shapes.
	 *
	 *  🔴 Why this exists: every pattern's presentation is Blueprint-side, so until the art is authored the patterns
	 *  are literally invisible and NONE of the PIE checks can be judged — you would only be able to observe "damage
	 *  arrived from nowhere". This lets the mechanics and the balance numbers be verified FIRST, and the art be built
	 *  against behaviour that is already known-good, rather than the other way round (which means rebuilding the art
	 *  when the numbers move).
	 *
	 *  Drawn on clients as well as the host, deliberately: the one thing a server-only overlay could never show is
	 *  whether the beam a CLIENT sees agrees with the beam the server is testing against. */
	FPSROGUELITE_API bool IsDebugDrawEnabled();
#endif
}
