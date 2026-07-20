// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// Closed-loop "storyteller" director — SENSOR layer types (P0a-0 walking skeleton, RunFlow §2-8-2).
//
// CONTRACT (design §8-1): P0 is a server-authoritative MEASUREMENT layer. It owns raw events, rolling
// counters, statistical transforms (rate/EWMA/deadband) and lifecycle ONLY. It does NOT own semantic
// judgement (never a "Score"), and it never changes spawns/missions/rewards/stats. Everything here is
// server-local and NON-replicated (plain C++ structs, no UPROPERTY/USTRUCT) — cheapest, zero PlayerState
// pollution. The math lives in PURE free functions (namespace FPSRTelemetry) so a single source of truth
// is golden-tested worldless, and PIE re-tuning of the constants can never break a relationship test.

class AActor;

/** How a player's incoming damage was caused. Only Enemy/Boss count toward IncomingDamageRate; every
 *  other class is excluded from the pressure signal (design: enemy/boss damage only — FF/self/env can't
 *  inflate pressure, which is the anti-exploit rule). Plain enum (server-internal; console parses int). */
enum class EFPSRDamageSourceClass : uint8
{
	Self = 0,        // instigator == victim (rocket-jump / explosion self-damage)
	Enemy,           // AFPSREnemyBase (incl. AFPSRRangedEnemyBase)
	Boss,            // AFPSRBossBase (separate bucket; ACharacter, NOT an AFPSREnemyBase)
	FriendlyFire,    // another AFPSRCharacter
	Door,            // AFPSRDoor (shares the enemy health component but is not a combatant)
	Mission,         // AFPSRMissionActor
	Env,             // null instigator / anything else (falling, world, unclassified)
	Count
};

/** Tunables for the movement-confinement (C1) state machine. Values are tunable (design ranges); the
 *  golden tests assert RELATIONSHIPS, not these magic numbers, so re-tuning never breaks a test. */
struct FFPSRConfinementParams
{
	/** Micro-jitter below this (cm) does not update the "last significant sample" bookkeeping. (75-100) */
	float Deadband = 90.0f;
	/** Stay within this radius (cm) of the anchor for ConfinementWindow seconds => confined. (600-800) */
	float ConfinementRadius = 700.0f;
	/** Hysteresis: once confined, the player must leave a LARGER radius (× this factor) to release. (1.25) */
	float ReleaseFactor = 1.25f;
	/** Seconds within the radius before confinement latches. (20-30) */
	float ConfinementWindow = 25.0f;

	float ReleaseRadius() const { return ConfinementRadius * ReleaseFactor; }
};

/** Fixed-size per-player confinement state (design §8-4 — exactly the 7 anchor fields, NO growing
 *  structure: no visited-cell set, no position history, no heatmap). AnchorStartTime < 0 = uninitialized
 *  (seeded on the first sample). XY only. */
struct FFPSRConfinementState
{
	FVector2D AnchorXY = FVector2D::ZeroVector;
	float AnchorStartTime = -1.0f;      // < 0 => not yet seeded
	FVector2D LastSampleXY = FVector2D::ZeroVector;
	float LastSampleTime = -1.0f;
	float MaxDistFromAnchor = 0.0f;
	bool bConfined = false;
	float ConfinedSince = -1.0f;        // < 0 => not confined
};

/** Fixed-size per-player telemetry (P0a-0 = exactly the 5 signals + interval accumulators + injection
 *  override scratch). Server-local, never replicated/serialized. */
struct FFPSRPlayerTelemetry
{
	// --- HealthPct ---
	float HealthPct = 1.0f;
	bool bHealthValid = false;

	// --- IncomingDamageRate[Enemy]/[Boss] (interval EWMA of accepted incoming damage) ---
	float IncomingDamageEnemyRateEwma = 0.0f;
	float IncomingDamageBossRateEwma = 0.0f;
	// Raw damage accumulated by the hook between Advances; converted to a rate + cleared each Advance.
	float EnemyDamageAccum = 0.0f;
	float BossDamageAccum = 0.0f;

	// --- DownedRecent01 (B3) ---
	int32 DownedCount = 0;
	float LastDownedTime = -1.0f;       // sensor-clock stamp of the most recent down (< 0 => never)
	float DownedRecent01 = 0.0f;

	// --- MovementConfinement01 (C1) ---
	FFPSRConfinementState Confinement;
	float MovementConfinement01 = 0.0f;
	bool bMovementConfined = false;

	// --- FrontId (read-only occupancy snapshot = the committed PS->GetCurrentMapId) ---
	FGameplayTag FrontId;

	// --- Injection scratch (#if !UE_BUILD_SHIPPING harness; harmless floats otherwise) ---
	float ForcedHealthPct = -1.0f;      // >= 0 => forced HealthPct while ForcedHealthUntil > SensorClock
	float ForcedHealthUntil = -1.0f;

	/** Per-Advance integration for ONE player (single source of truth — the subsystem and the
	 *  determinism golden both call this). Pure math over already-sampled inputs (health/pawn I/O is
	 *  done by the caller). Does NOT touch FrontId (set by the caller directly from the occupancy snapshot). */
	void Integrate(float Dt, float SensorNow, float RateAlpha, float DownedWindow,
		const FFPSRConfinementParams& CP,
		bool bHasHealth, float HealthPctIn,
		bool bHasSample, const FVector2D& SampleXY);
};

namespace FPSRTelemetry
{
	// ---- Pure statistical helpers (header-inline; no engine-heavy deps) -------------------------------

	/** Interval-EWMA of a rate: instantaneous rate = accum/dt, blended into the previous EWMA by Alpha.
	 *  Dt<=0 => neutral (instantaneous 0). Zero accum decays the EWMA toward 0; constant accum converges
	 *  toward accum/dt. */
	inline float StepEwmaRate(float PrevEwma, float IntervalAccum, float Dt, float Alpha)
	{
		const float InstRate = (Dt > 0.0f) ? (IntervalAccum / Dt) : 0.0f;
		return FMath::Lerp(PrevEwma, InstRate, FMath::Clamp(Alpha, 0.0f, 1.0f));
	}

	/** 0..1 recency ramp for a down: 1 at the moment of the down, linearly to 0 at Window, 0 past it or
	 *  if never downed (LastDownedTime < 0). */
	inline float ComputeDownedRecent01(float LastDownedTime, float Now, float Window)
	{
		if (LastDownedTime < 0.0f)
		{
			return 0.0f;
		}
		const float Age = Now - LastDownedTime;
		return FMath::Clamp(1.0f - Age / FMath::Max(Window, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	}

	/** The single freeze/authority gate predicate: advance only on the server, in an active run, unpaused.
	 *  Freeze (bPaused) => false => the sensor clock, EWMA and confinement window do NOT progress. */
	inline bool ShouldAdvance(bool bAuthority, bool bRunActive, bool bPaused)
	{
		return bAuthority && bRunActive && !bPaused;
	}

	/** Advance the confinement state machine by one sample. Anchor-radius + hysteresis, XY only. */
	inline void AdvanceConfinement(FFPSRConfinementState& S, const FVector2D& SampleXY, float Now,
		const FFPSRConfinementParams& P)
	{
		if (S.AnchorStartTime < 0.0f)
		{
			// First sample: seed the anchor epoch here.
			S.AnchorXY = SampleXY;
			S.AnchorStartTime = Now;
			S.LastSampleXY = SampleXY;
			S.LastSampleTime = Now;
			S.MaxDistFromAnchor = 0.0f;
			S.bConfined = false;
			S.ConfinedSince = -1.0f;
			return;
		}

		// Deadband: only record a NEW "last significant sample" when the step exceeds the deadband; tiny
		// jitter is ignored so a nearly-stationary player keeps a stable last-sample reference.
		const float StepDist = FVector2D::Distance(SampleXY, S.LastSampleXY);
		if (StepDist >= P.Deadband)
		{
			S.LastSampleXY = SampleXY;
			S.LastSampleTime = Now;
		}

		// Distance from the confinement anchor. Hysteresis: a NON-confined player resets on leaving the
		// confinement radius; a CONFINED player must leave the larger release radius (no boundary flicker).
		const float DistFromAnchor = FVector2D::Distance(SampleXY, S.AnchorXY);
		const float ExitRadius = S.bConfined ? P.ReleaseRadius() : P.ConfinementRadius;
		if (DistFromAnchor > ExitRadius)
		{
			// Left the region: start a fresh anchor epoch here (confinement clock restarts).
			S.AnchorXY = SampleXY;
			S.AnchorStartTime = Now;
			S.MaxDistFromAnchor = 0.0f;
			S.bConfined = false;
			S.ConfinedSince = -1.0f;
			return;
		}

		S.MaxDistFromAnchor = FMath::Max(S.MaxDistFromAnchor, DistFromAnchor);
		if (!S.bConfined && (Now - S.AnchorStartTime) >= P.ConfinementWindow)
		{
			S.bConfined = true;
			S.ConfinedSince = Now;
		}
	}

	/** 0..1 confinement level = fraction of the confinement window spent within the radius (clamped).
	 *  0 before the anchor is seeded. Reaches 1 exactly when confinement latches. */
	inline float ConfinementLevel01(const FFPSRConfinementState& S, float Now, const FFPSRConfinementParams& P)
	{
		if (S.AnchorStartTime < 0.0f)
		{
			return 0.0f;
		}
		return FMath::Clamp((Now - S.AnchorStartTime) / FMath::Max(P.ConfinementWindow, KINDA_SMALL_NUMBER),
			0.0f, 1.0f);
	}

	/** Only enemy/boss damage is "incoming pressure" (design anti-exploit: FF/self/door/mission/env excluded). */
	inline bool CountsAsIncoming(EFPSRDamageSourceClass Src)
	{
		return Src == EFPSRDamageSourceClass::Enemy || Src == EFPSRDamageSourceClass::Boss;
	}

	/** Classify the damage causer for a hit on Victim. Victim is used ONLY for the Self identity check.
	 *  Defined in the .cpp (needs the concrete actor classes). Order matters: Self before FF (both are
	 *  AFPSRCharacter); Boss before Enemy (boss is an ACharacter, not an AFPSREnemyBase). */
	FPSROGUELITE_API EFPSRDamageSourceClass ClassifyDamageSource(const AActor* Instigator, const AActor* Victim);

	/** Remove entries whose weak key has gone invalid (tombstone leak 0). Returns the number pruned.
	 *  Template so the golden test can exercise it worldless with plain UObject keys + CollectGarbage. */
	template <typename KeyType>
	int32 PruneInvalidTelemetry(TMap<TWeakObjectPtr<KeyType>, FFPSRPlayerTelemetry>& Map)
	{
		TArray<TWeakObjectPtr<KeyType>> Dead;
		for (const TPair<TWeakObjectPtr<KeyType>, FFPSRPlayerTelemetry>& Pair : Map)
		{
			if (!Pair.Key.IsValid())
			{
				Dead.Add(Pair.Key);
			}
		}
		for (const TWeakObjectPtr<KeyType>& Key : Dead)
		{
			Map.Remove(Key);
		}
		return Dead.Num();
	}
}

inline void FFPSRPlayerTelemetry::Integrate(float Dt, float SensorNow, float RateAlpha, float DownedWindow,
	const FFPSRConfinementParams& CP,
	bool bHasHealth, float HealthPctIn,
	bool bHasSample, const FVector2D& SampleXY)
{
	// HealthPct (sampled by the caller from the ASC or forced injection).
	if (bHasHealth)
	{
		HealthPct = FMath::Clamp(HealthPctIn, 0.0f, 1.0f);
		bHealthValid = true;
	}
	else
	{
		bHealthValid = false;
	}

	// IncomingDamageRate[Enemy]/[Boss]: convert the interval accumulator to a rate, EWMA it, then clear.
	IncomingDamageEnemyRateEwma = FPSRTelemetry::StepEwmaRate(IncomingDamageEnemyRateEwma, EnemyDamageAccum, Dt, RateAlpha);
	IncomingDamageBossRateEwma = FPSRTelemetry::StepEwmaRate(IncomingDamageBossRateEwma, BossDamageAccum, Dt, RateAlpha);
	EnemyDamageAccum = 0.0f;
	BossDamageAccum = 0.0f;

	// DownedRecent01: recency ramp off the last-down stamp (sensor clock — frozen during a freeze).
	DownedRecent01 = FPSRTelemetry::ComputeDownedRecent01(LastDownedTime, SensorNow, DownedWindow);

	// MovementConfinement01 (only when a position sample is available this Advance).
	if (bHasSample)
	{
		FPSRTelemetry::AdvanceConfinement(Confinement, SampleXY, SensorNow, CP);
		MovementConfinement01 = FPSRTelemetry::ConfinementLevel01(Confinement, SensorNow, CP);
		bMovementConfined = Confinement.bConfined;
	}
}
