// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGA_HomingOrbs.h"

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossHomingOrb.h"
#include "Combat/FPSRTargeting.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UFPSRBossGA_HomingOrbs::UFPSRBossGA_HomingOrbs()
{
}

void UFPSRBossGA_HomingOrbs::ServerBeginExecute()
{
	SpawnAccumulatorSeconds = 0.0f;
	NumOrbsSpawned = 0;
	ChosenTarget = nullptr;

	AFPSRBossBase* Boss = GetBoss();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !World)
	{
		return;
	}

	if (!OrbClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] HomingOrbs has no OrbClass assigned — nothing to launch. Assign it on the pattern BP."));
		return;
	}

	// One hunted player, picked once. Random among the survivors rather than nearest, so repeated casts spread the
	// pressure around the party instead of always punishing whoever happens to be closest to the boss.
	TArray<APawn*, TInlineAllocator<4>> Candidates;
	const float Clock = Boss->GetPatternClockSeconds();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!FPSRTargeting::IsEligibleTarget(PC, Clock, /*bRequireTopologyAck=*/false))
		{
			continue;
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			Candidates.Add(Pawn);
		}
	}
	if (Candidates.Num() == 0)
	{
		return;
	}
	ChosenTarget = Candidates[FMath::RandHelper(Candidates.Num())];

	// 🔴 Announced to EVERYONE, not just the target. That is what turns "one player is hunted" into a party problem
	// — the other three have to decide whether to shoot the orbs down or clear away from them.
	Boss->ServerSetMarkedPlayer(ChosenTarget.Get());
}

bool UFPSRBossGA_HomingOrbs::ServerTickExecute(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss || !OrbClass || !ChosenTarget.IsValid())
	{
		return true;
	}

	SpawnAccumulatorSeconds += DeltaSeconds;

	// while, not if: a frame hitch that swallows more than one interval must not swallow the orbs with it.
	while (NumOrbsSpawned < OrbCount && SpawnAccumulatorSeconds >= SpawnIntervalSeconds)
	{
		SpawnAccumulatorSeconds -= SpawnIntervalSeconds;
		SpawnOrb(Boss, NumOrbsSpawned++);
	}

	// Done as soon as the flight is away — the orbs live on their own. Holding the ability open until they resolve
	// would park the boss doing nothing for the whole chase, which reads as the boss being broken rather than as a
	// deliberate pause.
	return NumOrbsSpawned >= OrbCount;
}

void UFPSRBossGA_HomingOrbs::ServerEndExecute()
{
	// The mark comes down with the pattern. Leaving it up would keep a player flagged as hunted long after the orbs
	// that were hunting them are gone.
	if (AFPSRBossBase* Boss = GetBoss())
	{
		Boss->ServerSetMarkedPlayer(nullptr);
	}
}

void UFPSRBossGA_HomingOrbs::SpawnOrb(AFPSRBossBase* Boss, int32 Index)
{
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	APawn* Target = ChosenTarget.Get();
	if (!World || !OrbClass)
	{
		return;
	}

	// Evenly around the boss. The ring is the read: "something is forming up", visible from any angle, before the
	// flight commits to whoever is marked.
	const float AngleDeg = OrbCount > 0 ? (360.0f * Index) / OrbCount : 0.0f;
	const FVector Offset = FRotator(0.0f, AngleDeg, 0.0f).RotateVector(FVector::ForwardVector) * SpawnRingRadiusCm;
	const FVector Origin = Boss->GetActorLocation() + Offset + FVector(0.0f, 0.0f, SpawnHeightCm);

	// Face the target from the outset so the hover already points where it is going to go.
	const FVector Facing = Target ? (Target->GetActorLocation() - Origin).GetSafeNormal() : -Offset.GetSafeNormal();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Boss;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AFPSRBossHomingOrb* Orb = World->SpawnActor<AFPSRBossHomingOrb>(OrbClass, Origin, Facing.Rotation(), SpawnParams);
	if (!Orb)
	{
		return;
	}

	FFPSRBossOrbLaunchParams LaunchParams;
	LaunchParams.GraceSeconds = OrbGraceSeconds;
	LaunchParams.TrackSeconds = TrackSeconds;
	LaunchParams.Health = OrbHealth;
	LaunchParams.BlastDamage = Damage;
	LaunchParams.BlastRadiusCm = BlastRadiusCm;
	LaunchParams.BlastKnockback = BlastKnockback;

	FFPSRDamageSpec Spec;
	Spec.DamageType = DamageType;
	Orb->ServerLaunch(Boss, Target, LaunchParams, Spec);

	// Registering hands the boss two responsibilities it already owns for everything else it spawns: pushing the
	// freeze edge in, and destroying it if the boss dies or the run restarts.
	Boss->ServerRegisterPatternActor(Orb);
}
