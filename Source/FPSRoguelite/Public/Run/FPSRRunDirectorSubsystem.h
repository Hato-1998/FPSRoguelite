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
	/** Enter the pre-combat hold: spawns stay off until every player finishes their opening-seed picks (§2-2). */
	void BeginOpeningHold();
	/** True if every connected player has completed its opening seed. bOutAnyStarted = at least one started. */
	bool AreOpeningSeedsComplete(bool& bOutAnyStarted) const;
	void BeginRound(int32 Index);
	void EndRound();
	void TryResumeFromBreather();
	void SpawnRoundMission();
	void OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess);
	void EnterBoss();
	void DestroyActiveMission();
	FFPSRRoundDef GetRoundDef(int32 Index) const;
	int32 GetNumRounds() const;
	/** Pick where a round's mission spawns: weighted-random among designer-placed, tag-matched, enabled
	 *  AFPSRMissionSpawnPoints. Falls back to a player location when no matching point exists. */
	FTransform SelectMissionSpawnTransform(const UFPSRMissionDataAsset* Mission) const;
	/** True if at least one player controller currently possesses a pawn (run start gate). */
	bool HasAnyPlayerPawn() const;
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
	/** Set when StartRun is called before any player pawn exists; round 0 begins once one appears. */
	bool bAwaitingFirstPlayer = false;

	/** Pre-combat hold: true while waiting for players to finish opening-seed picks before round 0 spawns (§2-2). */
	bool bAwaitingOpeningSeed = false;
	float OpeningHoldElapsed = 0.0f;

	FTimerHandle DirectorTimerHandle;

	static constexpr float DirectorInterval = 0.25f;
	/** Safety: force combat to start if no player has even begun an opening seed within this many seconds
	 *  (e.g. a client whose UI never reports ready), so the run can never hard-lock at the pre-combat hold. */
	static constexpr float OpeningHoldNoStartTimeout = 30.0f;
	/** Safety: force combat after this long even if some player never finishes (hung/AFK client). */
	static constexpr float OpeningHoldMaxTimeout = 180.0f;
};
