// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPSRLobbyGameMode.generated.h"

class APlayerController;

/** Lobby hub game mode (P7 §3-3). Players gather here between runs: pick a loadout, invite friends, and the host
 *  starts the run. Seamless-travels to the gameplay map on start, and resets each player's run state on entry so
 *  a returning party starts a fresh run (P7 §3-6). Uses the project PlayerState (loadout/run-state carry) and a
 *  dedicated lobby PlayerController (lobby UI only — no gameplay HUD / card flow). */
UCLASS()
class FPSROGUELITE_API AFPSRLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRLobbyGameMode();

	/** Server: host-gated request to start the run. Validates the requester is the host (listen-server local
	 *  controller), then seamless ServerTravels everyone to the gameplay (run) map. */
	void RequestStartRun(APlayerController* Requester);

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

private:
	/** Reset the player's per-run progression to a fresh-run baseline (idempotent; server authority). */
	void ResetPlayerRunState(AController* C) const;
};
