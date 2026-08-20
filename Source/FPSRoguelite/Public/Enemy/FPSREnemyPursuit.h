// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"

/** ADR 0008 (Docs/Architecture/0008-hover-enemy-pursuit-reachability-modes.md): which steering/vertical mode a
 *  hovering enemy is in this pass. Flow = the normal flow-field XY + v2 terrain-relative Z (the everyday case —
 *  invariant 1's single source of routing). Seek3D = the reachability-escape mode opened ONLY when the flow can't
 *  route to a target (bFlowZero) or the enemy has stalled without landing an attack: straight-line XY toward the
 *  player + an incrementally-climbing altitude target (invariant 3 — no yo-yo, escalation is monotone per episode)
 *  until the forward sweep clears, then a glide descent while still chasing in a straight line. Plain C++ enum
 *  (server-side pursuit state, not BP-exposed — mirrors EFPSRFieldQuery's precedent in FPSRFlowFieldComputer.h). */
enum class EFPSRPursuitMode : uint8
{
	Flow,
	Seek3D,
};

/** Archetype-authored tuning for FFPSRPursuitState::Tick, copied each pass from AFPSREnemyBase's EditDefaultsOnly
 *  UPROPERTYs (category FPSR|Enemy|Seek3D — invariant 4: archetype variety is DATA, never a code branch). Kept as
 *  a separate value type — not embedded in the actor — so the state machine stays UObject-free and unit-testable
 *  without spinning up an actor (FPSRFlowFieldUnitTest.cpp's worldless-core precedent). */
struct FFPSRPursuitParams
{
	/** T (seconds): how long "no attack success + insufficient net movement" must persist before a stall trips
	 *  Seek3D (invariant 3's trigger, ADR's "결과 기반" trigger — catches corner-jams/separation-deadlocks the
	 *  flow-zero check alone can't enumerate). */
	float StallTime = 3.0f;
	/** D (cm): minimum net displacement inside the stall window that counts as "still making progress" — below
	 *  this, the window keeps accumulating toward StallTime instead of resetting. */
	float StallMinMove = 150.0f;
	/** Altitude gained (cm) per blocked-forward-sweep escalation step while climbing in Seek3D. */
	float ClimbStep = 120.0f;
	/** Absolute cap (cm, added on top of the target-relative seek altitude) the climb escalation can reach —
	 *  the "flight ceiling" invariant 1 references (bounds unlimited-altitude sniping). */
	float ClimbCeiling = 1200.0f;
	/** Seconds the forward sweep must stay UNBLOCKED before ClimbOffset decays one step — the "cleared the
	 *  obstacle, now glide down while still chasing" phase of the user-proposed trajectory. */
	float ClearDecayTime = 0.5f;
	/** Hysteresis floor (seconds, invariant 6): once a mode is entered, how long before a transition OUT of it is
	 *  allowed — prevents tick-to-tick mode flapping at a reachability/stall boundary. */
	float ModeMinHold = 1.0f;
};

/** ADR 0008 pursuit state machine core — pure data + a pure Tick function, no UObject/actor dependency, so
 *  FPSRoguelite.Enemy.Pursuit exercises the exact transition logic headless (no world, no NewObject). One instance
 *  per hovering enemy (AFPSREnemyBase::PursuitState); Activate() calls Reset() for pool reuse (no state leaks
 *  across lives). Server-only — this is pure math, no replication concern of its own. */
struct FFPSRPursuitState
{
	EFPSRPursuitMode Mode = EFPSRPursuitMode::Flow;
	/** Remaining hysteresis hold (seconds, invariant 6) before Mode may transition again. */
	float ModeHoldRemaining = 0.0f;
	/** Seconds elapsed in the current stall-detection window (invariant 3's anchor timer). Runs continuously
	 *  regardless of Mode — Seek3D's "pinned at the ceiling and STILL stalled" surplus-descent check reads it too. */
	float StallWindowElapsed = 0.0f;
	/** World-space anchor the stall window measures net displacement from; reset whenever real progress (or a
	 *  recent attack) is detected. */
	FVector StallAnchorLoc = FVector::ZeroVector;
	/** Seek3D altitude escalation accumulated this episode (cm). Monotonically non-decreasing WHILE blocked
	 *  (invariant 3 — no yo-yo: re-blocking after a partial clear resumes from the current value, never from 0). */
	float ClimbOffset = 0.0f;
	/** Seconds the forward sweep has been continuously unblocked — drives ClimbOffset's decay-by-one-step. */
	float ClearElapsed = 0.0f;

	/** Pool-reuse reset (AFPSREnemyBase::Activate) — back to Flow, every timer/accumulator zeroed. Idempotent. */
	void Reset();

	/** Advance the state machine by one movement pass. DeltaSeconds is already stride-scaled (ScaledDeltaSeconds —
	 *  the LOD-throttled per-enemy dt, NOT the raw frame delta) so the stall/climb timers age at the enemy's own
	 *  update cadence, not real wall-clock time. CurrentLoc is this pass's actor location (the stall-window
	 *  displacement anchor). bHasTarget/bFlowZero mirror the movement pass's own target/flow resolution for this
	 *  enemy. bForwardBlocked is the PREVIOUS pass's horizontal-sweep blocking result (a one-pass lag is
	 *  ADR-accepted — ticks a few cm apart). LastAttackTime/Now are AFPSREnemyBase::LastAttackTime and the world
	 *  time this pass, so a recent attack success folds into the stall condition for free (no separate branch —
	 *  ADR "공격 성공은 정체 윈도를 자동 무효화"). */
	void Tick(float DeltaSeconds, const FVector& CurrentLoc, bool bHasTarget, bool bFlowZero, bool bForwardBlocked,
		float LastAttackTime, float Now, const FFPSRPursuitParams& Params);
};
