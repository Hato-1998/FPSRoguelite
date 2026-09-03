// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGA_SweepLaser.h"

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossLaserMath.h"
#include "Combat/FPSRTargeting.h"
#include "Components/CapsuleComponent.h"
#include "Core/FPSRPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UFPSRBossGA_SweepLaser::UFPSRBossGA_SweepLaser()
{
}

bool UFPSRBossGA_SweepLaser::GetPawnBearing(const AFPSRBossBase* Boss, const APawn* Pawn, float& OutDeg, float& OutDistanceCm)
{
	if (!Boss || !Pawn)
	{
		return false;
	}
	const FVector Offset = Pawn->GetActorLocation() - Boss->GetActorLocation();
	const float DistanceXY = Offset.Size2D();
	if (DistanceXY <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutDeg = FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X));
	OutDistanceCm = DistanceXY;
	return true;
}

void UFPSRBossGA_SweepLaser::SetWarningActive(AFPSRBossBase* Boss, bool bActive)
{
	if (bWarningActive == bActive || !Boss)
	{
		return;
	}
	bWarningActive = bActive;

	UWorld* World = Boss->GetWorld();
	if (!World)
	{
		return;
	}

	// Reuse of the ranged swarm's existing Client/Reliable warning RPC — zero new RPCs, exactly as Enemy.md §2-6
	// records for ranged enemies. Keyed by the boss's unique id so it can never collide with a swarm shooter's.
	const int32 SourceId = static_cast<int32>(Boss->GetUniqueID());
	const FVector SourceLocation = Boss->GetActorLocation();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
		{
			PC->ClientNotifyRangedTarget(SourceId, SourceLocation, bActive);
		}
	}
}

void UFPSRBossGA_SweepLaser::ServerBeginExecute()
{
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss)
	{
		return;
	}

	BeamTrackByPawn.Reset();

	// Beam count and period are frozen for the whole cast. If the phase rose mid-sweep the period would change under
	// the tracked relative angles, and every stored PrevRel would suddenly be a value from a different domain.
	ActiveBeamCount = FMath::Clamp(Boss->GetCurrentPhase() * BeamsPerPhase, 1, FMath::Max(1, MaxBeams));

	// Speed comes from the phase table (clamped to the last entry), so "it spins faster later" is a number a designer
	// writes down rather than one they have to solve for out of a multiplier.
	ActiveSpeedDegPerSec = 30.0f;
	if (AngularSpeedByPhase.Num() > 0)
	{
		const int32 Index = FMath::Clamp(Boss->GetCurrentPhase() - 1, 0, AngularSpeedByPhase.Num() - 1);
		ActiveSpeedDegPerSec = AngularSpeedByPhase[Index];
	}
	if (FMath::Abs(ActiveSpeedDegPerSec) <= KINDA_SMALL_NUMBER)
	{
		ActiveSpeedDegPerSec = 30.0f; // a zero-speed sweep would never complete a revolution and never end
	}

	// First beam at a random cardinal (12/3/6/9); the rest evenly spaced from it — see the header for why every beam
	// is NOT pinned to a cardinal.
	StartAngleDeg = FPSRBossLaser::RandomCardinalDeg();
	StartClock = Boss->GetPatternClockSeconds();
	GraceEndClock = StartClock + BeamGraceSeconds;
	SweepDurationSeconds = BeamGraceSeconds + (360.0f * Revolutions) / FMath::Abs(ActiveSpeedDegPerSec);

	// Published once. Clients recompute the angle from these values plus the clock (never by integrating), so their
	// beam and the server's hit test cannot drift apart.
	Boss->ServerSetBeamState(ActiveBeamCount, StartAngleDeg, ActiveSpeedDegPerSec, StartClock, GraceEndClock, BeamVisualHeightCm);

	SetWarningActive(Boss, true);
}

bool UFPSRBossGA_SweepLaser::ServerTickExecute(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !World)
	{
		return true;
	}

	// 🔴 ONE time source. An earlier draft accumulated DeltaSeconds for the grace/duration while deriving the beam
	// ANGLE from the clock — two freeze-correct sources that can still drift apart, because the combat clock keeps
	// running through a stage transition while this tick does not.
	const float Clock = Boss->GetPatternClockSeconds();
	const float Elapsed = Clock - StartClock;
	const bool bLive = Clock >= GraceEndClock;
	if (bLive)
	{
		// The warning comes down the moment the beam starts moving and biting. Leaving it up through the damaging
		// phase would teach players that the warning means nothing.
		SetWarningActive(Boss, false);
	}

	const float Period = 360.0f / FMath::Max(1, ActiveBeamCount);
	const float HalfPeriod = Period * 0.5f;
	const float BeamAngle = FPSRBossLaser::BeamBaseAngleAt(StartAngleDeg, ActiveSpeedDegPerSec, GraceEndClock, Clock);

	// Drop pawns that went away, so the map cannot grow across a long fight.
	for (auto It = BeamTrackByPawn.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FFPSRDamageSpec Spec;
	Spec.DamageType = DamageType;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!FPSRTargeting::IsEligibleTarget(PC, Clock, /*bRequireTopologyAck=*/false))
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		float Bearing = 0.0f;
		float DistanceCm = 0.0f;
		if (!GetPawnBearing(Boss, Pawn, Bearing, DistanceCm))
		{
			continue;
		}

		const float CurRel = FPSRBossLaser::WrapToPeriod(Bearing - BeamAngle, Period);

		FBeamTrack& Track = BeamTrackByPawn.FindOrAdd(Pawn);
		if (!Track.bSeeded)
		{
			// Seed from this frame so the very first sample can never look like a crossing. Someone standing inside a
			// beam the instant it appears is covered by the exposure test below, not by a fabricated edge.
			Track.PrevRel = CurRel;
			Track.bSeeded = true;
		}

		// Widen by the capsule's angular size: close to the boss a thin beam visibly covers the whole capsule, and a
		// hit test that ignored that would disagree with the picture right where the player is watching it.
		float CapsuleRadius = FallbackCapsuleRadiusCm;
		if (const ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			}
		}
		const float HalfWidth = BeamHalfWidthDeg + FPSRBossLaser::CapsuleAngularRadiusDeg(CapsuleRadius, DistanceCm);

		// Exposed = inside the band now, or stepped clean over it since last frame.
		const bool bExposed = FMath::Abs(CurRel) <= HalfWidth
			|| FPSRBossLaser::DidEnterBeam(Track.PrevRel, CurRel, HalfPeriod, HalfWidth);

		const bool bGateOpen = bLive
			&& DistanceCm >= InnerRadiusCm
			&& !Boss->WasRecentlyAirborne(Pawn);

		if (bExposed)
		{
			// The latch — not the edge — is what makes this one hit per pass. Testing the gates only on the edge frame
			// would drop the whole pass whenever a gate opens while the player is already inside the band: the grace
			// ending on top of someone, or someone landing inside a beam. The second of those directly contradicts
			// "jump INTO the beam and you get hit".
			if (!Track.bLatched && bGateOpen)
			{
				Boss->ServerScheduleLaserHit(Pawn, Damage, Spec);
				Track.bLatched = true;
			}
		}
		else
		{
			Track.bLatched = false;
		}

		Track.PrevRel = CurRel;
	}

	return Elapsed >= SweepDurationSeconds;
}

void UFPSRBossGA_SweepLaser::ServerEndExecute()
{
	// Both of these come down on EVERY exit, including the boss dying mid-sweep — the base routes a cancel through
	// here too. A published beam keeps rendering on clients, and a warning left up sticks on the HUD for the rest of
	// the run.
	if (AFPSRBossBase* Boss = GetBoss())
	{
		SetWarningActive(Boss, false);
		Boss->ServerSetBeamState(0, 0.0f, 0.0f, 0.0f, 0.0f, BeamVisualHeightCm);
	}
	BeamTrackByPawn.Reset();
}
