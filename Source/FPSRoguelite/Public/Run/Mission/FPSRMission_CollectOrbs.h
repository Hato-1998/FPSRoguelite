// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_CollectOrbs.generated.h"

class AFPSRMissionOrb;
class APawn;
class AFPSRMissionPointSet;

/** Mission: collect every orb the mission spawns at its configured relative locations. Self-contained
 *  (spawns its own orbs), so it is testable without level placement. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_CollectOrbs : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_CollectOrbs();

	/** Orb class to spawn (content BP for mesh/VFX). Falls back to the C++ base when unset.
	 *  fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|CollectOrbs")
	TSubclassOf<AFPSRMissionOrb> OrbClass;

	/** Orb spawn offsets (cm) from the mission location. If empty, a small default set is used for testing.
	 *  fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|CollectOrbs")
	TArray<FVector> OrbRelativeLocations;

	virtual bool UsesPointSet() const override { return true; }
	virtual void AssignPointSet(AFPSRMissionPointSet* InSet) override { PointSet = InSet; }

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleOrbCollected(AFPSRMissionOrb* Orb, APawn* Collector);

	UPROPERTY()
	TArray<TObjectPtr<AFPSRMissionOrb>> SpawnedOrbs;

	/** Server-only: optional designer point set; when assigned, orbs spawn at its points instead of OrbRelativeLocations. */
	UPROPERTY()
	TObjectPtr<AFPSRMissionPointSet> PointSet = nullptr;

	int32 TotalOrbs = 0;
	int32 CollectedOrbs = 0;
};
