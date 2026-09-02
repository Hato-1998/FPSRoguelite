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

void UFPSRBossGA_HomingOrbs::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AFPSRBossBase* Boss = GetBoss();
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !Boss->HasAuthority() || !World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// InstancedPerActor: reset every per-cast counter, or the second cast starts mid-flight.
	SpawnAccumulatorSeconds = 0.0f;
	NumOrbsSpawned = 0;
	ChosenTarget = nullptr;

	if (!OrbClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] HomingOrbs has no OrbClass assigned — nothing to launch. Assign it on the pattern BP."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// One hunted player, picked once (see the header). Random among the survivors rather than nearest, so repeated
	// casts spread the pressure around the party instead of always punishing whoever happens to be closest.
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
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	ChosenTarget = Candidates[FMath::RandHelper(Candidates.Num())];

	// First orb goes immediately; the rest follow on the interval, so activation is visible right away.
	SpawnOrb(Boss, NumOrbsSpawned++);
	if (NumOrbsSpawned >= OrbCount)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UFPSRBossGA_HomingOrbs::ServerTickPattern(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss)
	{
		return;
	}

	SpawnAccumulatorSeconds += DeltaSeconds;

	// while, not if: a frame hitch that swallows more than one interval must not swallow the orbs with it.
	while (NumOrbsSpawned < OrbCount && SpawnAccumulatorSeconds >= SpawnIntervalSeconds)
	{
		SpawnAccumulatorSeconds -= SpawnIntervalSeconds;
		SpawnOrb(Boss, NumOrbsSpawned++);
	}

	if (NumOrbsSpawned >= OrbCount)
	{
		// Done as soon as the flight is away — the orbs live on their own (header).
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
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

	// Fan the launch directions across SpawnSpreadDeg, centred on the target's bearing. With one orb that is simply
	// straight at them.
	const FVector Origin = Boss->GetActorLocation() + FVector(0.0f, 0.0f, SpawnHeightCm);
	FVector Forward = Boss->GetActorForwardVector();
	if (Target)
	{
		const FVector ToTarget = Target->GetActorLocation() - Origin;
		if (!ToTarget.IsNearlyZero())
		{
			Forward = ToTarget.GetSafeNormal();
		}
	}

	const float Fraction = OrbCount > 1 ? (static_cast<float>(Index) / static_cast<float>(OrbCount - 1)) - 0.5f : 0.0f;
	const FRotator Spread(0.0f, Fraction * SpawnSpreadDeg, 0.0f);
	const FVector Direction = Spread.RotateVector(Forward);

	FActorSpawnParameters Params;
	Params.Owner = Boss;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AFPSRBossHomingOrb* Orb = World->SpawnActor<AFPSRBossHomingOrb>(OrbClass, Origin, Direction.Rotation(), Params);
	if (!Orb)
	{
		return;
	}

	FFPSRDamageSpec Spec;
	Spec.DamageType = DamageType;
	Orb->ServerLaunch(Boss, Target, OrbHealth, TrackSeconds, Damage, Spec);

	// Registering hands the boss two responsibilities it already owns for everything else it spawns: pushing the
	// freeze edge in, and destroying it if the boss dies or the run restarts.
	Boss->ServerRegisterPatternActor(Orb);
}
