// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "FPSRLobbyGameMode.generated.h"

class APlayerController;
class AController;

/** Lobby hub game mode (P7 §3-3). Players gather here between runs: pick a loadout, invite friends, and ready up.
 *  The run starts when every participant is ready (U11a — no host-only "Start"): the server arms a short countdown
 *  and then seamless-travels everyone to the gameplay map. Resets each player's run state on entry so a returning
 *  party starts a fresh run (P7 §3-6). Uses the project PlayerState (loadout/run-state carry) and a dedicated lobby
 *  PlayerController (lobby UI only — no gameplay HUD / card flow). */
UCLASS()
class FPSROGUELITE_API AFPSRLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRLobbyGameMode();

	/** Server: re-evaluate the all-ready start gate. Called whenever a player's ready state changes or the roster
	 *  changes (join/leave). Arms the start countdown when every participant is ready (>=1), cancels it otherwise. */
	void NotifyReadyChanged();

	/** Seconds remaining on the start countdown, or 0 if not counting down (UI hint). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	float GetReadyCountdownRemaining() const;

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	/** Seconds between "everyone ready" and the run start, giving players a beat to back out (un-ready cancels it). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Lobby")
	float ReadyStartCountdown = 3.0f;

private:
	/** Reset the player's per-run progression to a fresh-run baseline (idempotent; server authority). */
	void ResetPlayerRunState(AController* C) const;

	/** True if there is at least one participant and every participant is ready. OutParticipants = participant count. */
	bool AreAllParticipantsReady(int32& OutParticipants) const;

	/** Server: seamless ServerTravel everyone to the gameplay (run) map. Fired by the ready countdown. */
	void StartRunNow();

	/** Countdown timer armed once everyone is ready; fires StartRunNow on expiry. */
	FTimerHandle ReadyStartTimer;
};
