// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyPursuit.h"
#include "Math/UnrealMathUtility.h"

void FFPSRPursuitState::Reset()
{
	Mode = EFPSRPursuitMode::Flow;
	ModeHoldRemaining = 0.0f;
	StallWindowElapsed = 0.0f;
	StallAnchorLoc = FVector::ZeroVector;
	ClimbOffset = 0.0f;
	ClearElapsed = 0.0f;
}

void FFPSRPursuitState::Tick(float DeltaSeconds, const FVector& CurrentLoc, bool bHasTarget, bool bFlowZero,
	bool bForwardBlocked, float LastAttackTime, float Now, const FFPSRPursuitParams& Params)
{
	if (DeltaSeconds <= 0.0f)
	{
		return; // frozen / zero-dt pass — nothing ages (mirrors AFPSREnemyBase's freeze-paused accumulators)
	}

	// Hysteresis countdown (invariant 6) — ages every pass regardless of Mode, so a hold armed on entry into
	// Seek3D is already ticking down before the FIRST re-evaluation of the exit condition below.
	if (ModeHoldRemaining > 0.0f)
	{
		ModeHoldRemaining = FMath::Max(0.0f, ModeHoldRemaining - DeltaSeconds);
	}

	// --- Stall window (invariant 3's trigger, AND the Seek3D "still stuck at the ceiling" surplus-descent input
	//     below) — a rolling window whose anchor resets on real progress (net displacement >= StallMinMove) OR a
	//     recent attack success (folds "attack landed" into the stall condition for free — ADR: no separate
	//     branch needed). Runs unconditionally, independent of Mode, since both transitions read it. ---
	const bool bRecentAttack = (Now - LastAttackTime) <= Params.StallTime;
	bool bStalled = false;
	if (!bHasTarget)
	{
		// No target to stall against — keep the window primed at "just reset" so acquiring a target starts clean
		// instead of inheriting a stale elapsed count from a prior targetless stretch.
		StallWindowElapsed = 0.0f;
		StallAnchorLoc = CurrentLoc;
	}
	else if (bRecentAttack || FVector::DistSquared(CurrentLoc, StallAnchorLoc) > FMath::Square(Params.StallMinMove))
	{
		StallWindowElapsed = 0.0f;
		StallAnchorLoc = CurrentLoc;
	}
	else
	{
		StallWindowElapsed += DeltaSeconds;
		bStalled = StallWindowElapsed >= Params.StallTime;
	}

	if (Mode == EFPSRPursuitMode::Flow)
	{
		// -> Seek3D (ADR decision, verbatim): flow-zero fires IMMEDIATELY (a proven unreachable column — no reason
		// to wait out the stall window); a stall (T elapsed, D undercut, no recent attack) fires the same way. Not
		// gated by ModeHoldRemaining — a genuinely unreachable/stalled enemy must escape without waiting on a hold
		// armed by an unrelated EARLIER transition (only the Seek3D->Flow direction is hold-gated, below).
		if (bHasTarget && (bFlowZero || bStalled))
		{
			Mode = EFPSRPursuitMode::Seek3D;
			ModeHoldRemaining = Params.ModeMinHold;
			ClimbOffset = 0.0f;
			ClearElapsed = 0.0f;
			// Fresh episode: restart the stall window from here too, so the Seek3D "pinned + still stalled" check
			// below measures from the moment the escape began, not the already-expired window that triggered it.
			StallWindowElapsed = 0.0f;
			StallAnchorLoc = CurrentLoc;
		}
	}
	else // Seek3D
	{
		// Escalation (invariant 3 — monotone while blocked, no yo-yo): a blocked forward sweep raises the climb
		// target one step (capped at ClimbCeiling); a sustained CLEAR sweep decays it one step at a time instead
		// of dropping instantly — the "cleared the obstacle, now glide down while still chasing" phase (ADR's
		// user-proposed trajectory). Re-blocking after a partial clear resumes from the CURRENT ClimbOffset, never
		// from 0, so the same obstacle can't be re-tried at the same (failed) altitude twice.
		if (bForwardBlocked)
		{
			ClimbOffset = FMath::Min(ClimbOffset + Params.ClimbStep, Params.ClimbCeiling);
			ClearElapsed = 0.0f;
		}
		else
		{
			ClearElapsed += DeltaSeconds;
			if (ClearElapsed >= Params.ClearDecayTime)
			{
				ClimbOffset = FMath::Max(ClimbOffset - Params.ClimbStep, 0.0f);
				ClearElapsed = 0.0f;
			}
		}

		// Surplus-descent escape (ADR "잉여 하강" failure flow, invariant 3): pinned at the ceiling AND STILL
		// stalled (the T-second window elapsed again while maxed out) — climbing higher isn't solving it (e.g. a
		// sealed rooftop over the target). Give up the climb entirely: ClimbOffset back to 0 turns off the seek-Z
		// override, so the v2 glide spring re-lands on whatever terrain is actually under the enemy. If THAT
		// landing regains flow, the next Tick's bFlowZero==false (once the hold below expires) returns to Flow —
		// the tunnel-mouth re-entry the ADR's failure-flow scenario describes, instead of circling forever.
		if (ClimbOffset >= Params.ClimbCeiling && bStalled)
		{
			ClimbOffset = 0.0f;
			ClearElapsed = 0.0f;
			StallWindowElapsed = 0.0f;
			StallAnchorLoc = CurrentLoc;
		}

		// -> Flow: the flow is alive again (a reachable column under/near the enemy) AND the entry hold has
		// expired (invariant 6 — prevents an immediate flicker back out the tick after entering Seek3D).
		if (!bFlowZero && ModeHoldRemaining <= 0.0f)
		{
			Mode = EFPSRPursuitMode::Flow;
			ModeHoldRemaining = Params.ModeMinHold;
			ClimbOffset = 0.0f;
			ClearElapsed = 0.0f;
		}
	}
}
