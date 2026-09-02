// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGA_SweepLaser.h"

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossLaserMath.h"
#include "Combat/FPSRTargeting.h"
#include "Components/CapsuleComponent.h"
#include "Core/FPSRPlayerController.h"
#include "Engine/World.h"
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

float UFPSRBossGA_SweepLaser::ComputeStartAngle(const AFPSRBossBase* Boss) const
{
	const UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!World || StartAngleRule == EFPSRBeamStartAngle::ArenaFixed)
	{
		return 0.0f;
	}

	const float Period = 360.0f / FMath::Max(1, ActiveBeamCount);

	// Fold every survivor's bearing into ONE beam period first: with N equally spaced beams, two players a period
	// apart are in the same place as far as the beams are concerned, so the gap has to be measured in that domain or
	// the "widest gap" would be a gap that does not actually exist.
	TArray<float, TInlineAllocator<4>> Bearings;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!FPSRTargeting::IsEligibleTarget(PC, Boss->GetPatternClockSeconds(), /*bRequireTopologyAck=*/false))
		{
			continue;
		}
		float Deg = 0.0f;
		float Distance = 0.0f;
		if (GetPawnBearing(Boss, PC->GetPawn(), Deg, Distance))
		{
			Bearings.Add(FPSRBossLaser::WrapToPeriod(Deg, Period));
		}
	}

	if (Bearings.Num() == 0)
	{
		return 0.0f;
	}

	Bearings.Sort();

	// Widest circular gap on the folded domain, including the wrap-around gap between last and first.
	float BestGap = (Bearings[0] + Period) - Bearings.Last(); // the wrap-around gap
	float BestStart = Bearings.Last();
	for (int32 Index = 1; Index < Bearings.Num(); ++Index)
	{
		const float Gap = Bearings[Index] - Bearings[Index - 1];
		if (Gap > BestGap)
		{
			BestGap = Gap;
			BestStart = Bearings[Index - 1];
		}
	}

	return FPSRBossLaser::WrapToPeriod(BestStart + BestGap * 0.5f, Period);
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

void UFPSRBossGA_SweepLaser::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Committing is what stamps the freeze-paused cooldown — skipping it leaves this pattern permanently off cooldown.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AFPSRBossBase* Boss = GetBoss();
	if (!Boss || !Boss->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// InstancedPerActor: this object survives between activations, so every piece of per-cast state resets here.
	BeamTrackByPawn.Reset();

	// Frozen for the whole cast — see the header on why the period must not move under the tracked angles.
	ActiveBeamCount = FMath::Clamp(Boss->GetCurrentPhase() * BeamsPerPhase, 1, FMath::Max(1, MaxBeams));

	const float Speed = FMath::Abs(AngularSpeedDegPerSec) > KINDA_SMALL_NUMBER ? AngularSpeedDegPerSec : 30.0f;
	SweepDurationSeconds = WarmupSeconds + (360.0f * Revolutions) / FMath::Abs(Speed);

	StartAngleDeg = ComputeStartAngle(Boss);
	StartClock = Boss->GetPatternClockSeconds();

	// Publish once. Clients recompute the angle from these values plus the clock (never by integrating), so their
	// beam and the server's hit test cannot drift apart.
	Boss->ServerSetBeamState(ActiveBeamCount, StartAngleDeg, Speed, StartClock, StartClock + WarmupSeconds);

	// The warning goes up for the warmup window: the beams are visible but harmless, which is the "charge delay +
	// indicator" Enemy.md §2-6 demands of anything that hits without a dodgeable projectile.
	SetWarningActive(Boss, true);
}

void UFPSRBossGA_SweepLaser::ServerTickPattern(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !World)
	{
		return;
	}

	// 🔴 ONE time source. An earlier draft accumulated DeltaSeconds for the warmup/duration while deriving the beam
	// ANGLE from the clock — two freeze-correct sources that can still drift apart, because the combat clock keeps
	// running through a stage transition while this tick does not. A boss-phase transition cannot happen today (the
	// stage director refuses one), so that was latent rather than live; deriving everything from the clock removes
	// the question instead of relying on that refusal staying true.
	const float Clock = Boss->GetPatternClockSeconds();
	const float Elapsed = Clock - StartClock;
	const bool bWarmupOver = Elapsed >= WarmupSeconds;
	if (bWarmupOver)
	{
		// Warning comes down the moment the beams become real — leaving it up through the damaging phase would train
		// players to ignore it.
		SetWarningActive(Boss, false);
	}

	const float Period = 360.0f / FMath::Max(1, ActiveBeamCount);
	const float HalfPeriod = Period * 0.5f;
	const float BeamAngle = FPSRBossLaser::BeamBaseAngleAt(StartAngleDeg, AngularSpeedDegPerSec, StartClock, Clock);

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
			// Seed from this frame so the very first sample can never look like a crossing. Someone who is standing
			// inside a beam the instant it appears is covered by the exposure test below, not by a fake edge.
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

		const bool bGateOpen = bWarmupOver
			&& DistanceCm >= InnerRadiusCm
			&& !Boss->WasRecentlyAirborne(Pawn);

		if (bExposed)
		{
			// The latch — not the edge — is what makes this one hit per pass. Checking the gates on the edge frame
			// alone would drop the whole pass whenever a gate opens while the player is already inside the band
			// (warmup ending on someone, or landing inside a beam).
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

	if (Elapsed >= SweepDurationSeconds)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UFPSRBossGA_SweepLaser::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// EVERY exit runs through here, including the boss's death-time CancelAbilities. Both of these have to be undone
	// unconditionally: a beam left published keeps rendering on clients, and a warning left up sticks on the HUD for
	// the rest of the run.
	if (AFPSRBossBase* Boss = GetBoss())
	{
		SetWarningActive(Boss, false);
		Boss->ServerSetBeamState(0, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	BeamTrackByPawn.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
