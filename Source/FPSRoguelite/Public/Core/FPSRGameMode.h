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

	/** Called when the run ends (victory or defeat). Authority-only.
	 *  Notifies all players with their individual result (ClientShowRunResult RPC). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void EndRun(EFPSRRunOutcome Outcome);

	/** Server: number of living (non-spectator) participants. Independent aggregation function — U9 (DBNO) needs
	 *  only re-define AFPSRPlayerState::IsAlive(); this counter and the wipe predicate stay. Material for the
	 *  P7 §3-6 'Wiped' check. */
	int32 GetLivingPlayerCount() const;

	/** Server: true when there is at least one participant and none are alive (party wiped). Guards against the
	 *  zero-participant transient (pre-travel / all left) so an empty PlayerArray is NOT read as a defeat. */
	bool AreAllPlayersDead() const;

	/** Server: called when a player dies. Ends the run in Defeat if the whole party is now wiped. */
	void NotifyPlayerDefeated();

protected:
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
	/** Guard: prevents EndRun from being called twice (RPC spam / race condition). */
	bool bRunEnded = false;
};
