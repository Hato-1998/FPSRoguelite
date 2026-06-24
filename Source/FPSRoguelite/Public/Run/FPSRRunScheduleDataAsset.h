// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRRunScheduleDataAsset.generated.h"

class UFPSRMissionDataAsset;
class UFPSRBossDefinitionDataAsset;

/** One scheduled mission window: at a random time within [MinTime, MaxTime] (rolled once at run start), one
 *  mission is chosen uniformly at random from MissionPool and spawned (Game.MD §2-8). */
USTRUCT(BlueprintType)
struct FFPSRMissionWindow
{
	GENERATED_BODY()

	/** Earliest run-clock time (seconds) this window can fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window", meta = (ClampMin = "0.0"))
	float MinTime = 60.0f;

	/** Latest run-clock time (seconds). Actual trigger = a random time in [MinTime, MaxTime], rolled at run
	 *  start. Set MinTime == MaxTime for an exact time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window", meta = (ClampMin = "0.0"))
	float MaxTime = 120.0f;

	/** Candidate missions — one is chosen uniformly at random when the window fires (empty = no-op). Restrict
	 *  the pool to control which missions can appear in this window (e.g. exclude HoldZone early). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window")
	TArray<TObjectPtr<UFPSRMissionDataAsset>> MissionPool;
};

/** Data-driven run schedule (redesign 2026-06-04 / windows 2026-06-11, §2-8): time-windowed mission spawns
 *  (each fires once at a random time in its range, picking a random mission from its pool) + boss time + a
 *  time-scaled enemy target count. No rounds — the run is continuous, frozen only for card selection. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRRunScheduleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Time-windowed missions (any order; the director rolls each window's trigger time at run start). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRMissionWindow> MissionWindows;

	/** Run-clock time (seconds) at which the boss appears (Combat -> Boss; after this no missions / no timer). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float BossTime = 300.0f;

	/** Boss to spawn at BossTime (which class + tuning). Null = the director spawns the C++ AFPSRBossBase
	 *  placeholder (so the victory loop is testable before boss content exists). Game.MD §2-7/§2-8. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TObjectPtr<UFPSRBossDefinitionDataAsset> BossDefinition;

	/** Target alive enemy count at run start (the spawn director's base intensity). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 BaseAliveCount = 40;

	/** Added to the target alive count per minute of survival (time-scaling difficulty), BEFORE the boss appears. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinute = 30.0f;

	/** Added per minute AFTER the boss appears — the swarm persists and keeps ramping at this (higher) rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinuteAfterBoss = 50.0f;

	/** Hard cap on the time-scaled target alive count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 MaxAliveCount = 300;

	/** Per director-tick spawn cap = the swarm FILL RATE (this x ~10/sec). Lower = enemies trickle in and the crowd
	 *  builds up / recovers gradually; higher = the swarm snaps to the target count fast. Tune for pacing feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "1"))
	int32 MaxSpawnPerTick = 3;
};
