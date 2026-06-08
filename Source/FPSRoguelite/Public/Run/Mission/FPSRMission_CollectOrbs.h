// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_CollectOrbs.generated.h"

class AFPSRMissionOrb;
class APawn;

/** Mission: collect every orb the mission spawns at its configured relative locations. Self-contained
 *  (spawns its own orbs), so it is testable without level placement. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_CollectOrbs : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_CollectOrbs();

	/** Orb class to spawn (content BP for mesh/VFX). Falls back to the C++ base when unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|CollectOrbs")
	TSubclassOf<AFPSRMissionOrb> OrbClass;

	/** Orb spawn offsets (cm) from the mission location. If empty, a small default set is used for testing. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|CollectOrbs")
	TArray<FVector> OrbRelativeLocations;

protected:
	virtual void OnMissionActivated() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleOrbCollected(AFPSRMissionOrb* Orb, APawn* Collector);

	UPROPERTY()
	TArray<TObjectPtr<AFPSRMissionOrb>> SpawnedOrbs;

	int32 TotalOrbs = 0;
	int32 CollectedOrbs = 0;
};
