// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_CollectOrbs.h"
#include "Run/Mission/FPSRMissionOrb.h"
#include "Engine/World.h"

AFPSRMission_CollectOrbs::AFPSRMission_CollectOrbs()
{
	PrimaryActorTick.TickInterval = 0.1f;
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

	// Default test layout when the designer has not authored offsets yet.
	TArray<FVector> Offsets = OrbRelativeLocations;
	if (Offsets.Num() == 0)
	{
		Offsets = { FVector(600.0f, 0.0f, 50.0f), FVector(0.0f, 600.0f, 50.0f), FVector(-600.0f, 0.0f, 50.0f) };
	}

	const FVector Origin = GetActorLocation();
	UClass* SpawnClass = OrbClass ? OrbClass.Get() : AFPSRMissionOrb::StaticClass();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FVector& Offset : Offsets)
	{
		AFPSRMissionOrb* NewOrb = World->SpawnActor<AFPSRMissionOrb>(SpawnClass, Origin + Offset, FRotator::ZeroRotator, Params);
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

void AFPSRMission_CollectOrbs::OnMissionEnded(bool bSuccess)
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
