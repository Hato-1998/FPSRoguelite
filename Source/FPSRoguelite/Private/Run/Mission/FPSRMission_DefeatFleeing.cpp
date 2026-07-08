// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_DefeatFleeing.h"
#include "Run/Mission/FPSRMissionFleeTarget.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AFPSRMission_DefeatFleeing::AFPSRMission_DefeatFleeing()
{
	PrimaryActorTick.TickInterval = 0.05f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_DefeatFleeing::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_DefeatFleeing::StaticClass();
}

void AFPSRMission_DefeatFleeing::OnMissionActivated()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// §2-8-1: read from the DataAsset's Tuning (or the tuning subclass's CDO defaults when unset).
	// FleeSpeed/FleeTriggerRange are read every tick, so cache them; TargetClass is only needed here at spawn.
	const UFPSRMissionTuning_DefeatFleeing& T = GetTuning<UFPSRMissionTuning_DefeatFleeing>();
	EffFleeSpeed = T.FleeSpeed;
	EffFleeTriggerRange = T.FleeTriggerRange;
	const TSubclassOf<AFPSRMissionFleeTarget> EffTargetClass = T.TargetClass;

	UClass* SpawnClass = EffTargetClass ? EffTargetClass.Get() : AFPSRMissionFleeTarget::StaticClass();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	Target = World->SpawnActor<AFPSRMissionFleeTarget>(SpawnClass, GetActorLocation(), GetActorRotation(), Params);
	if (Target)
	{
		if (UFPSREnemyHealthComponent* HC = Target->GetHealthComponent())
		{
			HC->OnDeath.AddDynamic(this, &AFPSRMission_DefeatFleeing::HandleTargetDeath);
		}
	}
	SetMissionProgress(0.0f);
}

void AFPSRMission_DefeatFleeing::OnMissionTickServer(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	// Find the nearest player pawn.
	const FVector TargetLoc = Target->GetActorLocation();
	APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				const float DSq = FVector::DistSquared(TargetLoc, P->GetActorLocation());
				if (DSq < NearestDistSq)
				{
					NearestDistSq = DSq;
					Nearest = P;
				}
			}
		}
	}

	// Flee directly away from the nearest player while they are close (sweep so it does not tunnel walls).
	if (Nearest && NearestDistSq <= EffFleeTriggerRange * EffFleeTriggerRange)
	{
		FVector Away = TargetLoc - Nearest->GetActorLocation();
		Away.Z = 0.0f;
		if (Away.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			const FVector Dir = Away.GetSafeNormal();
			Target->AddActorWorldOffset(Dir * EffFleeSpeed * DeltaSeconds, true);
			Target->SetActorRotation(Dir.Rotation());
		}
	}

	// Progress reflects damage dealt (1 - remaining health fraction).
	if (UFPSREnemyHealthComponent* HC = Target->GetHealthComponent())
	{
		const float MaxHP = FMath::Max(HC->GetMaxHealth(), 1.0f);
		SetMissionProgress(FMath::Clamp(1.0f - HC->GetHealth() / MaxHP, 0.0f, 1.0f));
	}
}

void AFPSRMission_DefeatFleeing::HandleTargetDeath(AActor* DeadActor, AActor* Killer)
{
	CompleteMission();
}

void AFPSRMission_DefeatFleeing::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up the spawned target on ANY teardown (not just mission-ended), so a direct mission destruction
	// does not orphan it. Server owns the target.
	if (HasAuthority() && Target)
	{
		Target->Destroy();
		Target = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}
