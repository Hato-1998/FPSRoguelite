// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRLobbyPlayerController.generated.h"

class UFPSRPrimaryGameLayout;
class UCommonActivatableWidget;

/** Lobby player controller (P7 §3-3): creates the primary layout and pushes the lobby widget (mirrors the menu
 *  PC — UI only, no gameplay HUD / card flow). Hosts the server-authoritative lobby RPCs: loadout selection and
 *  the host-only start-run request. On the run start the engine seamlessly swaps this for the gameplay PC. */
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

	/** Server RPC (client intent): request the run start. The GameMode gates this to the host. */
	UFUNCTION(Server, Reliable)
	void ServerRequestStartRun();

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
