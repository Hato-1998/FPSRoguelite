// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Sweeping-laser geometry (BOSS1 pattern 2). Header-only and stateless on purpose — the same shape as
 *  namespace FPSRVitals (VIT1): pure O(1) arithmetic that can be unit-tested with no world, no actors and no
 *  replication, which is what makes the hit rule verifiable at all.
 *
 *  🔴 There is no trace anywhere in this pattern. The beam is defined by an angle, so "did the beam cross this
 *  player" is an angle question; a swept shape query would cost far more and answer the same thing less exactly. */
namespace FPSRBossLaser
{
	/** Fold an angle into [-Period/2, +Period/2).
	 *
	 *  With Period = 360/N, N equally spaced beams collapse onto ONE comparison: the beams are indistinguishable in
	 *  that domain, so the per-player work is independent of how many beams a phase has. */
	FORCEINLINE float WrapToPeriod(float Deg, float Period)
	{
		if (Period <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		const float Half = Period * 0.5f;
		return FMath::Fmod(FMath::Fmod(Deg + Half, Period) + Period, Period) - Half;
	}

	/** One beam pass = one hit.
	 *
	 *  🔴 This is deliberately NOT "is the player inside the beam right now". A level test fires on every frame the
	 *  player is inside the band, so a slow relative sweep (0.13 deg/frame against a 6 deg band) would land ~46
	 *  consecutive hits and the real hit count would then be decided by the player's invulnerability window —
	 *  i.e. balance would be hostage to an unrelated tuning value.
	 *
	 *  🔴 And it is not "did the beam's swept arc contain the player's current angle" either. That test only looks
	 *  at where the PLAYER is now, so once the player moves it mis-measures by the player's own angular step: at
	 *  900 cm/s (BaseWalkSpeed) close to the boss, it misses well over half of all crossings, and a player moving
	 *  with the sweep can be missed entirely while a player behind the beam is hit.
	 *
	 *  So: gate on "was outside last frame", then accept EITHER of the two ways a pass can look —
	 *    - entered the band (a slow pass, seen over several frames), or
	 *    - flipped sign without ever sampling inside it (a fast pass that stepped straight over the band).
	 *  Dropping the second half is what silently breaks fast sweeps: |Cur| > HalfWidth would simply never be true.
	 *
	 *  Cases, all correct: slow pass 1 · fast pass 1 · reversing inside the band 0 (already counted) · leaving and
	 *  re-entering 1 (a genuine second exposure) · first frame with Prev == Cur 0.
	 *
	 *  @param PrevRel/CurRel  WrapToPeriod(player angle - beam angle) from the previous and current frame.
	 *  @param MaxStep         Period/2. A jump larger than this is the domain wrapping, not a crossing.
	 *  @param HalfWidthDeg    Beam half width + the player's angular radius (CapsuleAngularRadiusDeg). */
	FORCEINLINE bool DidEnterBeam(float PrevRel, float CurRel, float MaxStep, float HalfWidthDeg)
	{
		if (FMath::Abs(CurRel - PrevRel) > MaxStep)
		{
			return false; // domain wrap — not a crossing
		}
		if (FMath::Abs(PrevRel) <= HalfWidthDeg)
		{
			return false; // already inside last frame — this pass is already counted
		}
		return FMath::Abs(CurRel) <= HalfWidthDeg || ((PrevRel < 0.0f) != (CurRel < 0.0f));
	}

	/** The angular half-size a player capsule occupies at distance Distance.
	 *  Added to the beam's own half width so a beam that visibly clips the capsule also registers — close to the
	 *  boss a thin beam covers a lot of capsule, and without this the visual and the hit disagree exactly where the
	 *  player is most likely to be looking at it. */
	FORCEINLINE float CapsuleAngularRadiusDeg(float CapsuleRadiusCm, float DistanceCm)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(CapsuleRadiusCm / FMath::Max(DistanceCm, 1.0f), 0.0f, 1.0f)));
	}

	/** Beam angle at a given clock. Server and clients call THIS, rather than each integrating a delta — two
	 *  integrators started from the same value drift apart, a closed form cannot. */
	FORCEINLINE float BeamBaseAngleAt(float StartAngleDeg, float SpeedDegPerSec, float StartClock, float NowClock)
	{
		return StartAngleDeg + SpeedDegPerSec * (NowClock - StartClock);
	}
}
