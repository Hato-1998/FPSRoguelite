// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRRunScheduleDataAsset.generated.h"

class UFPSRMissionDataAsset;

/** One time-scheduled mission event: a mission spawns when the run clock reaches TriggerTime (Game.MD §2-8). */
USTRUCT(BlueprintType)
struct FFPSRMissionEvent
{
	GENERATED_BODY()

	/** Run-clock time (seconds) at which this mission appears. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Event")
	float TriggerTime = 60.0f;

	/** The mission to spawn (null = no-op). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Event")
	TObjectPtr<UFPSRMissionDataAsset> Mission = nullptr;
};

/** Data-driven run schedule (redesign 2026-06-04, §2-8): time-based mission events + boss time + a
 *  time-scaled enemy target count. No rounds — the run is continuous, frozen only for card selection. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRRunScheduleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Time-scheduled missions (any order; the director fires each when the run clock passes its TriggerTime). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRMissionEvent> MissionEvents;

	/** Run-clock time (seconds) at which the boss appears (Combat -> Boss; after this no missions / no timer). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float BossTime = 300.0f;

	/** Target alive enemy count at run start (the spawn director's base intensity). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 BaseAliveCount = 40;

	/** Added to the target alive count per minute of survival (time-scaling difficulty). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinute = 30.0f;

	/** Hard cap on the time-scaled target alive count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 MaxAliveCount = 250;
};
