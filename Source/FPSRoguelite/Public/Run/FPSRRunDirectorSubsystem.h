// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "FPSRRunDirectorSubsystem.generated.h"

class AFPSRMissionActor;
class UFPSRMissionDataAsset;
class AFPSRGameState;
class UFPSREnemySpawnSubsystem;

/** Server-authoritative run progression director.
 *  Manages round timeline, mission spawning, and phase transitions (Combat -> Breather -> Boss).
 *  Uses a fallback schedule if none is provided. */
UCLASS()
class FPSROGUELITE_API UFPSRRunDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Set the run schedule (must be called before StartRun). */
	void SetActiveSchedule(UFPSRRunScheduleDataAsset* InSchedule) { ActiveSchedule = InSchedule; }

	/** Start the run: begin round 0 and activate the director timer loop. */
	void StartRun();

	// Debug/testing entry points
	void DebugForceEndRound();
	void DebugTriggerMission();
	void DebugClearMission();
	void DebugSkipToBoss();
	void SetTimeScale(float InScale) { TimeScale = FMath::Max(0.0f, InScale); }
	void SetRunDebug(bool bEnable) { bRunDebug = bEnable; }

private:
	bool HasServerAuthority() const;
	void DirectorTick();
	void BeginRound(int32 Index);
	void EndRound();
	void TryResumeFromBreather();
	void SpawnRoundMission();
	void OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess);
	void EnterBoss();
	void DestroyActiveMission();
	FFPSRRoundDef GetRoundDef(int32 Index) const;
	int32 GetNumRounds() const;
	FVector ComputeMissionLocation() const;
	AFPSRGameState* GetGS() const;
	UFPSREnemySpawnSubsystem* GetSpawnSub() const;

	// State
	UPROPERTY()
	TObjectPtr<UFPSRRunScheduleDataAsset> ActiveSchedule;

	TArray<FFPSRRoundDef> FallbackRounds;

	UPROPERTY()
	TObjectPtr<AFPSRMissionActor> ActiveMission;

	int32 CurrentRoundIndex = 0;
	float ElapsedInRound = 0.0f;
	float RoundDuration = 0.0f;
	float TotalElapsed = 0.0f;
	float MissionTriggerTime = -1.0f;
	bool bMissionSpawned = false;
	bool bMissionClearedThisRound = false;
	float TimeScale = 1.0f;
	bool bRunActive = false;
	bool bRunDebug = false;

	FTimerHandle DirectorTimerHandle;

	static constexpr float DirectorInterval = 0.25f;
};
