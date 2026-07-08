// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_CollectOrbs.h"
#include "Run/Mission/FPSRMissionOrb.h"
#include "Run/Mission/FPSRMissionPointSet.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Engine/World.h"

AFPSRMission_CollectOrbs::AFPSRMission_CollectOrbs()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_CollectOrbs::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_CollectOrbs::StaticClass();
}

void AFPSRMission_CollectOrbs::OnMissionActivated()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	SpawnedOrbs.Reset();
	CollectedOrbs = 0;

	// §2-8-1: read from the DataAsset's Tuning (or the tuning subclass's CDO defaults when unset).
	const UFPSRMissionTuning_CollectOrbs& T = GetTuning<UFPSRMissionTuning_CollectOrbs>();
	const TSubclassOf<AFPSRMissionOrb> EffOrbClass = T.OrbClass;
	const TArray<FVector>& EffOrbRelativeLocations = T.OrbRelativeLocations;

	// Spawn locations: a designer point set (world points) when assigned; otherwise fall back to relative
	// offsets from the mission location (a default test set when none authored) so it works without placement.
	TArray<FVector> SpawnLocations;
	if (PointSet)
	{
		PointSet->GetWorldPoints(SpawnLocations);
	}
	if (SpawnLocations.Num() == 0)
	{
		TArray<FVector> Offsets = EffOrbRelativeLocations;
		if (Offsets.Num() == 0)
		{
			Offsets = { FVector(600.0f, 0.0f, 50.0f), FVector(0.0f, 600.0f, 50.0f), FVector(-600.0f, 0.0f, 50.0f) };
		}
		const FVector Origin = GetActorLocation();
		for (const FVector& Offset : Offsets)
		{
			SpawnLocations.Add(Origin + Offset);
		}
	}

	UClass* SpawnClass = EffOrbClass ? EffOrbClass.Get() : AFPSRMissionOrb::StaticClass();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FVector& Loc : SpawnLocations)
	{
		AFPSRMissionOrb* NewOrb = World->SpawnActor<AFPSRMissionOrb>(SpawnClass, Loc, FRotator::ZeroRotator, Params);
		if (NewOrb)
		{
			NewOrb->OnCollectedNative.AddUObject(this, &AFPSRMission_CollectOrbs::HandleOrbCollected);
			SpawnedOrbs.Add(NewOrb);
		}
	}

	TotalOrbs = SpawnedOrbs.Num();
	SetMissionProgress(0.0f);
}

void AFPSRMission_CollectOrbs::HandleOrbCollected(AFPSRMissionOrb* Orb, APawn* Collector)
{
	if (!Orb)
	{
		return;
	}
	Orb->SetCollected(true);
	++CollectedOrbs;
	SetMissionProgress(TotalOrbs > 0 ? static_cast<float>(CollectedOrbs) / static_cast<float>(TotalOrbs) : 1.0f);

	if (CollectedOrbs >= TotalOrbs)
	{
		CompleteMission();
	}
}

void AFPSRMission_CollectOrbs::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up spawned orbs on ANY teardown (completion, fail, or a direct DestroyActiveMission — e.g. on boss
	// entry), not just the mission-ended path, so orbs never orphan with a dangling delegate. Server owns them.
	if (HasAuthority())
	{
		for (AFPSRMissionOrb* Orb : SpawnedOrbs)
		{
			if (Orb)
			{
				Orb->OnCollectedNative.RemoveAll(this);
				Orb->Destroy();
			}
		}
		SpawnedOrbs.Reset();
	}

	Super::EndPlay(EndPlayReason);
}
