// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "FPSRRunDirectorSubsystem.generated.h"

class AFPSRMissionActor;
class UFPSRMissionDataAsset;
class AFPSRGameState;
class UFPSREnemySpawnSubsystem;
class AFPSRMissionPointSet;

/** Server-authoritative run director (redesign 2026-06-04, Game.MD §2-8).
 *  No rounds — the run is continuous. The director advances a run clock (paused during the global card-
 *  selection freeze and after the boss), scales spawn intensity over time, spawns time-scheduled missions,
 *  and triggers the boss at BossTime. Mission clears grant a reward pick + freeze (handled via GameState). */
UCLASS()
class FPSROGUELITE_API UFPSRRunDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Set the run schedule (must be called before StartRun). Null = built-in fallback (test) values. */
	void SetActiveSchedule(UFPSRRunScheduleDataAsset* InSchedule) { ActiveSchedule = InSchedule; }

	/** Start the run: reset the clock and activate the director timer loop. */
	void StartRun();

	// Debug/testing entry points
	void DebugTriggerMission(int32 WindowIndex = -1, int32 PoolIndex = -1);
	void DebugClearMission();
	void DebugSkipToBoss();
	void SetTimeScale(float InScale) { TimeScale = FMath::Max(0.0f, InScale); }
	void SetRunDebug(bool bEnable) { bRunDebug = bEnable; }

private:
	bool HasServerAuthority() const;
	void DirectorTick();
	void UpdateSpawnIntensity();
	void TrySpawnDueMission();
	void SpawnMission(UFPSRMissionDataAsset* MissionData);
	void OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess);
	void EnterBoss();
	void DestroyActiveMission();

	/** Time-scaled target alive enemy count from the schedule (or fallback) at the current run clock. */
	int32 ComputeTargetAliveCount() const;
	float GetBossTime() const;

	/** Uniformly pick a non-null mission from the window's pool (null when the pool has none). */
	UFPSRMissionDataAsset* PickRandomMission(const FFPSRMissionWindow& Window) const;

	/** Pick where a mission spawns: weighted-random among designer-placed, tag-matched, enabled spawn points
	 *  (falls back to a player location when none exist). */
	FTransform SelectMissionSpawnTransform(const UFPSRMissionDataAsset* Mission) const;
	/** Pick which AFPSRMissionPointSet a point-set mission uses: weighted-random among enabled, tag-matched sets
	 *  (MinPlayerDistance measured to the set's first point). Null when none match. */
	AFPSRMissionPointSet* SelectMissionPointSet(const UFPSRMissionDataAsset* Mission) const;
	/** True if at least one player controller currently possesses a pawn (run start gate). */
	bool HasAnyPlayerPawn() const;
	/** True if every present FPSR player controller has had its opening seed issued (pre-combat hold gate). */
	bool AllPlayersOpeningSeedIssued() const;
	AFPSRGameState* GetGS() const;
	UFPSREnemySpawnSubsystem* GetSpawnSub() const;

	// State
	UPROPERTY()
	TObjectPtr<UFPSRRunScheduleDataAsset> ActiveSchedule;

	UPROPERTY()
	TObjectPtr<AFPSRMissionActor> ActiveMission;

	TArray<bool> MissionWindowFired;
	/** Per-window trigger time, rolled within [MinTime, MaxTime] at run start. */
	TArray<float> WindowTriggerTimes;

	float RunClock = 0.0f;
	float TimeScale = 1.0f;
	bool bRunActive = false;
	bool bRunDebug = false;
	bool bBossStarted = false;
	/** Set when StartRun is called before any player pawn exists; spawning begins once one appears. */
	bool bAwaitingFirstPlayer = false;
	/** Set after a pawn appears until the opening-seed freeze engages — holds spawning so enemies can't
	 *  appear before the run-start card selection (the freeze then gates spawning on its own). */
	bool bWaitingForOpeningSeed = false;
	float OpeningWaitElapsed = 0.0f;
	/** Next run-clock threshold (seconds) at which to log progress (every 30s of run time). */
	float NextRunLogTime = 30.0f;

	FTimerHandle DirectorTimerHandle;

	static constexpr float DirectorInterval = 0.25f;
	/** Max seconds to hold spawning waiting for the opening-seed freeze before proceeding anyway (anti-deadlock). */
	static constexpr float OpeningSeedWaitTimeout = 5.0f;

	// Fallback (test) schedule values when no schedule asset is assigned (no missions without content).
	static constexpr float FallbackBossTime = 300.0f;
	static constexpr int32 FallbackBaseAliveCount = 40;
	static constexpr float FallbackAliveCountPerMinute = 30.0f;
	static constexpr int32 FallbackMaxAliveCount = 250;
};
