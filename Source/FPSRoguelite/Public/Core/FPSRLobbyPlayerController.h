// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRLobbyPlayerController.generated.h"

class UFPSRPrimaryGameLayout;
class UCommonActivatableWidget;

/** Lobby player controller (P7 §3-3): creates the primary layout and pushes the lobby widget (mirrors the menu
 *  PC — UI only, no gameplay HUD / card flow). Hosts the server-authoritative lobby RPCs: loadout selection and
 *  the per-player ready toggle (U11a — the all-ready aggregation in the GameMode starts the run; there is no
 *  host-only "Start"). On the run start the engine seamlessly swaps this for the gameplay PC. */
UCLASS()
class FPSROGUELITE_API AFPSRLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server RPC (client intent): pick the loadout weapon at PoolIndex. The server validates the index against
	 *  the configured loadout pool (the client only ever sends an index) and stores it on the PlayerState. */
	UFUNCTION(Server, Reliable)
	void ServerSelectLoadoutWeapon(int32 PoolIndex);

	/** Server RPC (client intent): set this player's lobby ready state. The server applies it to the PlayerState
	 *  (guarded: ready requires a chosen weapon) and re-evaluates the all-ready start gate (U11a). */
	UFUNCTION(Server, Reliable)
	void ServerSetReady(bool bReady);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRPrimaryGameLayout> PrimaryLayoutClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UCommonActivatableWidget> LobbyWidgetClass;

private:
	/** Point the local view at the lobby's room-overview CameraActor (tag "LobbyCamera", else the first camera in
	 *  the level) and stop auto-managing the view target — otherwise a listen-server client's pawn possession steals
	 *  the view onto the (spectator) pawn, which sits inside the podium meshes. Retries until the camera is found
	 *  (level actors may lag a fresh client travel). No-op if the level has no CameraActor (engine default kept). */
	void ApplyLobbyViewTarget();

	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;

	/** Retry timer for ApplyLobbyViewTarget while the lobby camera hasn't replicated/loaded yet. */
	FTimerHandle LobbyViewTimer;

	/** Bounded retry counter for the lobby view-target lookup. */
	int32 LobbyViewRetries = 0;
};
