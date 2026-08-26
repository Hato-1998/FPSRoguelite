// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameModeBase.h"
#include "Templates/SubclassOf.h"
#include "Core/FPSRGameFlowTypes.h"
#include "FPSRGameMode.generated.h"

class UFPSRCardPoolDataAsset;
class UFPSRRunScheduleDataAsset;
class AFPSREnemyBase;

/** Default game mode wiring the project's GameState, PlayerController, PlayerState and Character. */
UCLASS()
class FPSROGUELITE_API AFPSRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRGameMode();

	virtual void BeginPlay() override;

	/** A participant left mid-run. The two run-state predicates that read PlayerArray — the wipe check
	 *  (NotifyPlayerDefeated) and the card-select freeze (GameState::RefreshPauseState) — are both PULL-based: they
	 *  only recompute when something calls them, and a disconnect calls neither on its own. Without this hook the
	 *  party soft-locks: the last survivor leaving never declares Defeat, and a leaver who still owed a card pick
	 *  freezes the run forever (the freeze stops the director/spawner/XP, so no later level-up or mission clear can
	 *  ever re-trigger the recompute — it seals itself). Mirrors AFPSRLobbyGameMode::Logout. */
	virtual void Logout(AController* Exiting) override;

	/** Route spawn/reconnect placement through the currently ACTIVE arena's own authored APlayerStart set (F9).
	 *  ADR 0010 D6 parks reserve arenas in the SAME level with rendering/collision off but their authored
	 *  APlayerStart actors still physically present — the engine default (AGameModeBase::
	 *  ChoosePlayerStart_Implementation) scans every PlayerStart in the whole level with no notion of "which arena
	 *  is live", so it can hand a run start or a reconnect a start point sitting in a currently-inactive arena:
	 *  invisible, and the player falls through the instant they land (that arena's floor collision is off). Falls
	 *  back to Super:: when there is no active arena or it authored no starts, same as before this override existed.
	 *  Mirrors AFPSRLobbyGameMode::ChoosePlayerStart_Implementation's per-seat override. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Called when the run ends (victory or defeat). Authority-only.
	 *  Notifies all players with their individual result (ClientShowRunResult RPC). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void EndRun(EFPSRRunOutcome Outcome);

	/** Server: number of PARTICIPANTS (PlayerArray entries that are not spectator-only) — independent of life
	 *  state, so it changes only on connect/disconnect, never on death/DBNO. Extracted from AreAllPlayersDead's own
	 *  inline count (신설 2026-08-26, this IS that same loop, pulled out) so a second caller — ADR 0010 D6's stage-
	 *  difficulty party-size lookup (UFPSRRunDirectorSubsystem::ApplyStageDifficultyToArena) — reads the SAME "who
	 *  counts as in this run" definition the wipe check already uses, rather than a second one that could disagree.
	 *  Deliberately NOT GetLivingPlayerCount(), below: IsAlive() excludes DBNO, and a stage-commit durability
	 *  lookup keyed off living count would let a party go 3-down/1-up right before the commit and revive right
	 *  after — flipping a 4-player run onto the 1-player durability rate for the rest of the stage. */
	int32 GetParticipantCount() const;

	/** Server: number of living (non-spectator) participants. Independent aggregation function — U9 (DBNO) needs
	 *  only re-define AFPSRPlayerState::IsAlive(); this counter and the wipe predicate stay. Material for the
	 *  P7 §3-6 'Wiped' check. */
	int32 GetLivingPlayerCount() const;

	/** Server: true when there is at least one participant and none are alive (party wiped). Guards against the
	 *  zero-participant transient (pre-travel / all left) so an empty PlayerArray is NOT read as a defeat. */
	bool AreAllPlayersDead() const;

	/** Server: called when a player dies. Ends the run in Defeat if the whole party is now wiped. */
	void NotifyPlayerDefeated();

	/** Server: called when the boss dies (the victory caller, U3). Ends the run in Victory — closing the loop the
	 *  same way as defeat (EndRun -> EndRunFreeze -> OnRunEnded -> lobby travel). EndRun's bRunEnded latch guards a
	 *  same-frame victory/defeat race. Mirrors NotifyPlayerDefeated; does NOT touch EndRun's body (U11a owns it). */
	void NotifyBossDefeated();

	/** Server: travel back to the lobby hub immediately (result-screen Return click). Cancels the pending
	 *  post-run auto-travel timer so the two paths can't double-fire, then reuses the same TravelToLobby. */
	void RequestReturnToLobby();

	/** Server: true once EndRun has latched (victory/defeat). Gates the manual return-to-lobby/menu RPCs so a client
	 *  can't trigger a mid-run travel before the result screen (W1 P2-3). */
	bool IsRunEnded() const { return bRunEnded; }

protected:
	/** Bound to GameState OnRunEnded (subscribed in BeginPlay): closes the loop by traveling back to the lobby
	 *  hub a short beat after the result screen shows. Deliberately a SEPARATE method (not in EndRun's body) so
	 *  the victory caller (U3) and this lobby-return caller stay on independent code paths (P7 §3-5). */
	UFUNCTION()
	void HandlePostRunTravel();

	/** Seamless ServerTravel back to the lobby hub (LobbyMap + "?listen"). Authority only. */
	void TravelToLobby();

	/** Delay (seconds) after the run ends before traveling back to the lobby, so the result screen is readable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Flow")
	float PostRunTravelDelay = 3.0f;

	/** Server-only timer for the post-run lobby travel. */
	FTimerHandle PostRunTravelTimer;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Cards")
	TObjectPtr<UFPSRCardPoolDataAsset> CardPool;

	/** Round/mission schedule for the run (assigned in the GameMode BP). If null the director uses a
	 *  built-in test fallback schedule (values only, no asset paths). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Run")
	TObjectPtr<UFPSRRunScheduleDataAsset> RunSchedule;

	/** Swarm enemy class to spawn (assign a BP child of AFPSREnemyBase to set XPReward/mesh/stats).
	 *  If null the spawn director falls back to the C++ AFPSREnemyBase (engine cube placeholder). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Enemy")
	TSubclassOf<AFPSREnemyBase> EnemyClass;

private:
	/** Guard: prevents EndRun from being called twice (RPC spam / race condition). Distinct from
	 *  AFPSRGameState::bRunEnded (the freeze latch) — this is the GameMode's one-shot EndRun guard (W1 P3-7). */
	bool bRunEnded = false;
};
