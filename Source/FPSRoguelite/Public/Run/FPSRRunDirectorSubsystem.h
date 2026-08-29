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
class AFPSRBossBase;
class AFPSRArenaActor;

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

	/** Cancel whatever mission is currently active, with NO success/failure consequence (no reward grant, no
	 *  "Mission failed" log) — a pure teardown, distinct from both a successful clear and a timeout/fail. Public
	 *  wrapper around the private DestroyActiveMission so a caller outside this subsystem (currently only
	 *  UFPSRStageDirectorSubsystem::PerformSwap, on an arena swap — ADR 0010 D6) can reach it without taking on the
	 *  rest of the mission lifecycle. Returns true if a mission was actually active (and is now gone); false is a
	 *  harmless no-op, so a caller that doesn't care may ignore the return value. */
	bool CancelActiveMission();

	/** Server: (re)apply the stage-difficulty durability axis (ADR 0010 D6 cost axis, 신설 2026-08-26) to Arena's
	 *  suppressors — effective durability = InhibitorBaseDurability x EvalStageAt(StageIndex).
	 *  InhibitorDurabilityMultiplier x EvalPartySizeMultiplier(participant count). No-op if ActiveSchedule or Arena
	 *  is null (an asset-less/world-less test run leaves every suppressor at its actor-authored Durability).
	 *  Participant count is read ONCE per call (AFPSRGameMode::GetParticipantCount — NOT GetLivingPlayerCount; see
	 *  the .cpp for why DBNO must still count as a participant here), so a mid-stage death/DBNO never re-scales an
	 *  already-committed suppressor's health out from under the fight. Public: called from BOTH this subsystem's
	 *  own DirectorTick (stage 0, the opening-seed release point) AND UFPSRStageDirectorSubsystem::PerformSwap
	 *  (stage N>=1, right after GS->SetStageIndex commits the new stage) — the same cross-subsystem shape
	 *  CancelActiveMission above already established via RequestTransition, not a new coupling. */
	void ApplyStageDifficultyToArena(AFPSRArenaActor* Arena, int32 StageIndex);

	// Debug/testing entry points
	void DebugTriggerMission(int32 WindowIndex = -1, int32 PoolIndex = -1);
	void DebugClearMission();
	void DebugSkipToBoss();
	void SetTimeScale(float InScale) { TimeScale = FMath::Max(0.0f, InScale); }
	void SetRunDebug(bool bEnable) { bRunDebug = bEnable; }

	/** Run-clock time (seconds) the boss appears at (from the schedule, or the fallback). Public so the enemy spawn
	 *  subsystem can time-scale contact damage to the run timeline. */
	float GetBossTime() const;

private:
	bool HasServerAuthority() const;
	void DirectorTick();
	void UpdateSpawnIntensity();
	void TrySpawnDueMission();
	void SpawnMission(UFPSRMissionDataAsset* MissionData);
	void OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess);
	void EnterBoss();

	/**
	 * BossTime has arrived: get the party INTO the boss arena before any boss exists (보스 스테이지, 2026-08-28).
	 *
	 * Returns true when the caller should stop here and let the stage transition run (a transition was just
	 * requested, or one is already in flight); false means "there is nothing to move to — spawn the boss where we
	 * stand", which is the pre-boss-stage behaviour and stays the behaviour for any world with no boss arena
	 * (test maps, automation worlds).
	 *
	 * Order matters and is not negotiable: PerformSwap teleports players and carries the swarm, but a boss is
	 * NEITHER — it would be left alone in the old arena with its collision switched off and the run could never
	 * reach victory or defeat. So the swap happens first, in Combat phase, and EnterBoss runs only once
	 * GetActiveArena() reports the boss arena.
	 *
	 * Bounded by BossStageResolveTimeoutSeconds of run-clock-frozen director ticks: if the boss arena never becomes
	 * reachable (package not loaded, transition rejected repeatedly, destination regenerate failure), it gives up
	 * LOUDLY and degrades to spawning the boss in place. A run that cannot end is worse than a boss in the wrong room.
	 */
	bool TryEnterBossStage();

	/** Ask the arena stream subsystem to make the boss arena visible ahead of BossTime (see
	 *  UFPSRRunScheduleDataAsset::BossArenaParkLeadSeconds). Idempotent and cheap; fires once per run. */
	void TryPreParkBossArena();

	/** StageOrder of the world's boss arena, or INDEX_NONE if it has none. Asked of the streaming ROSTER, not of
	 *  AFPSRArenaActor::FindAllInWorld — the boss arena is deliberately not visible yet when this is first asked. */
	int32 FindBossArenaStageOrder() const;

	/** True when the arena the run is standing in right now is authored as the boss arena. */
	bool IsInBossArena() const;
	/** Spawn the boss (BossDefinition->BossClass, or the C++ AFPSRBossBase fallback) at a boss spawn point and
	 *  apply its definition. Called by EnterBoss — which does NOT clear the swarm (it deliberately persists into the
	 *  boss fight; see EnterBoss). */
	void SpawnBoss();
	/** Pick where the boss spawns. When bUseSpawnPoint, weighted-random among enabled AFPSRBossSpawnPoint actors
	 *  (falling back to a player location + forward offset, with a warning, when none are placed). When false, the
	 *  definition opted out of spawn points — go straight to the player-relative fallback. The boss always appears. */
	FTransform SelectBossSpawnTransform(bool bUseSpawnPoint, UClass* BossClass) const;
	void DestroyActiveMission();

	/** Time-scaled target alive enemy count from the schedule (or fallback) at the current run clock. */
	int32 ComputeTargetAliveCount() const;

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

	/** The spawned boss (server). Held so it isn't GC'd and for future boss-phase logic (real boss unit). */
	UPROPERTY()
	TObjectPtr<AFPSRBossBase> ActiveBoss;

	TArray<bool> MissionWindowFired;
	/** Per-window trigger time, rolled within [MinTime, MaxTime] at run start. */
	TArray<float> WindowTriggerTimes;

	float RunClock = 0.0f;
	/** Seconds elapsed since the boss appeared — drives the continued post-boss spawn ramp while RunClock stays pinned
	 *  at BossTime (the survival/HUD clock stops at the boss). */
	float PostBossElapsed = 0.0f;
	float TimeScale = 1.0f;
	bool bRunActive = false;
	bool bRunDebug = false;
	bool bBossStarted = false;

	/** The boss-stage question is settled: either the party is standing IN the boss arena, or this world has none
	 *  (or the wait timed out). Latched so TryEnterBossStage stops asking the roster every tick once the answer
	 *  can no longer change, and so a post-swap tick goes straight to EnterBoss. Reset by StartRun. */
	bool bBossStageResolved = false;

	/** Seconds of director ticks spent trying to reach the boss arena, against BossStageResolveTimeoutSeconds.
	 *  Time-based rather than an attempt count because the thing being waited on (a level package loading, a
	 *  concurrent suppressor transition finishing) takes seconds, and a director tick is 0.25s — a "3 attempts"
	 *  budget would expire in under a second and degrade a perfectly healthy run to an in-place boss. */
	float BossStageWaitElapsed = 0.0f;

	/** `FPlatformTime::Seconds()` at the first boss-gate tick (0 = not entered yet). BossStageWaitElapsed is derived
	 *  from this rather than accumulated per tick — see the gate's comment for why accumulation silently turns the
	 *  30s budget into ~19 minutes of wall clock. */
	double BossStageWaitStartSeconds = 0.0;

	/** True once the boss arena's pre-park has been requested this run (see BossArenaParkLeadSeconds). */
	bool bBossArenaParkRequested = false;
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

	/** Max seconds to keep trying to reach the boss arena at BossTime before spawning the boss in place instead
	 *  (anti-deadlock, same shape as OpeningSeedWaitTimeout). Generous: it has to cover a boss-arena sublevel that
	 *  is still streaming in AND a suppressor transition that happened to start on the same tick, both of which are
	 *  legitimate multi-second waits. The run clock does not advance during this window, so the cost of being
	 *  generous is only that the boss appears a little later. */
	static constexpr float BossStageResolveTimeoutSeconds = 30.0f;

	/** How long to keep asking the roster for a boss arena before concluding this world simply has none. Short on
	 *  purpose: "not authored" and "package still loading" look identical from the roster, and the FIRST is by far
	 *  the common case (every map without a boss stage) — a long grace here would delay the boss on all of them. A
	 *  boss sublevel registered in the persistent level is loaded well inside this window. */
	static constexpr float BossArenaLookupGraceSeconds = 2.0f;

	// Fallback (test) schedule values when no schedule asset is assigned (no missions without content).
	static constexpr float FallbackBossTime = 300.0f;
	static constexpr int32 FallbackBaseAliveCount = 40;
	static constexpr float FallbackAliveCountPerMinute = 30.0f;
	static constexpr float FallbackAliveCountPerMinuteAfterBoss = 50.0f;
	static constexpr int32 FallbackMaxAliveCount = 300;
	static constexpr int32 FallbackMaxSpawnPerTick = 3;
	static constexpr float FallbackSpawnIntervalSeconds = 0.1f;
};
