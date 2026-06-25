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

/** One anchor in the level-driven alive-count curve: at party Level, the spawn director targets Count alive enemies.
 *  The director interpolates piecewise-linearly between anchors (author them in ascending Level order). */
USTRUCT(BlueprintType)
struct FFPSRAliveCountAnchor
{
	GENERATED_BODY()

	/** Party level (FPSRGameState::GetPartyLevel) at this anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alive Count", meta = (ClampMin = "1"))
	int32 Level = 1;

	/** Target alive enemy count at this level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alive Count", meta = (ClampMin = "0"))
	int32 Count = 10;
};

/** Data-driven run schedule (redesign 2026-06-04 / windows 2026-06-11, §2-8): time-windowed mission spawns
 *  (each fires once at a random time in its range, picking a random mission from its pool) + boss time + a
 *  level-scaled (preferred) or time-scaled enemy target count. No rounds — the run is continuous, frozen only for
 *  card selection. */
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

	/** Level-driven target alive count (preferred): piecewise-linear anchors over party level. When NON-EMPTY this
	 *  REPLACES the time ramp below — target = interp(GetPartyLevel()) (below the first anchor uses its Count, above
	 *  the last stays flat at its Count), clamped to MaxAliveCount. Empty = legacy time ramp (BaseAliveCount + …).
	 *  e.g. (1,10),(20,30),(30,50): density scales with progression, not the clock. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRAliveCountAnchor> AliveCountByLevel;

	/** Target alive enemy count at run start (the spawn director's base intensity). LEGACY time ramp — used only when
	 *  AliveCountByLevel is empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 BaseAliveCount = 40;

	/** Added to the target alive count per minute of survival, BEFORE the boss appears. LEGACY — used only when
	 *  AliveCountByLevel is empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinute = 30.0f;

	/** Added per minute AFTER the boss appears — the swarm persists and keeps ramping at this (higher) rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinuteAfterBoss = 50.0f;

	/** Hard cap on the time-scaled target alive count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 MaxAliveCount = 300;

	/** Per director-tick spawn cap = enemies spawned each director tick. Combined with SpawnIntervalSeconds this is the
	 *  swarm FILL RATE (MaxSpawnPerTick / SpawnIntervalSeconds per second). Lower = enemies trickle in and the crowd
	 *  builds up / recovers gradually; higher = the swarm snaps to the target count fast. Tune for pacing feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "1"))
	int32 MaxSpawnPerTick = 3;

	/** Director tick interval (seconds) = how OFTEN the swarm director spawns a batch. The per-second fill rate is
	 *  MaxSpawnPerTick / SpawnIntervalSeconds (e.g. 1 per 0.1s = 10/sec; 1 per 0.25s = 4/sec). Raise to slow the
	 *  spawn PACE without changing the target count. Tune for pacing feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.02"))
	float SpawnIntervalSeconds = 0.1f;
};
