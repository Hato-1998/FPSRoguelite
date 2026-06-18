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
	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;
};
