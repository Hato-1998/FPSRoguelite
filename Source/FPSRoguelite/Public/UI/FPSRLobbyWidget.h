// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "FPSRLobbyWidget.generated.h"

class UFPSRLoadoutPoolDataAsset;
class UFPSRWeaponDataAsset;

/** Lobby UI base (P7 §3-3/§3-8). The C++ base exposes the lobby actions (loadout pick, invite, host start) and
 *  read-only state for the WBP to build its player list / weapon list. Visuals are authored in the WBP child. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRLobbyWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Establish menu input mode (menu input, no mouse capture) — mirrors the main menu. */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	/** Pick the loadout weapon at the given pool index (routes to the server-authoritative lobby PC RPC). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void SelectLoadoutWeapon(int32 PoolIndex);

	/** Host-only request to start the run (the server gates this to the host). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void RequestStartRun();

	/** Open the Steam friend-invite overlay for the current session. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void RequestShowInvite();

	/** The configured selectable-weapon pool (same content on client and server). Null if unset. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	UFPSRLoadoutPoolDataAsset* GetLoadoutPool() const;

	/** This player's current loadout pick (null until chosen). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	UFPSRWeaponDataAsset* GetSelectedWeapon() const;

	/** True if the local player is the host (listen server / standalone) — gate the "Start" button on this. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	bool IsLocalPlayerHost() const;

	/** Fired when the local player's loadout selection changes — the WBP refreshes its highlight/state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Lobby")
	void OnLoadoutRefreshed();

private:
	/** Bind to the local PlayerState's OnLoadoutChanged; retries until the PlayerState exists (client replication). */
	bool TryBindPlayerState();

	UFUNCTION()
	void HandleLoadoutChanged();

	/** Retry timer for the PlayerState binding. */
	FTimerHandle BindRetryTimer;

	/** Whether OnLoadoutChanged is currently bound (so destruct can unbind cleanly). */
	bool bBoundToPlayerState = false;
};
