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

	/** Toggle the local player's ready state (U11a). The run auto-starts once every participant is ready — there is
	 *  no host-only "Start". Readying requires a chosen weapon (the server rejects it otherwise). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void ToggleReady();

	/** Open the Steam friend-invite overlay for the current session. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void RequestShowInvite();

	/** Join a lobby by its 6-char code (U11a). Tears down the local session first if hosting/joined, then searches
	 *  for the advertised code and joins the first match. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Lobby")
	void JoinLobbyByCode(const FString& Code);

	/** The configured selectable-weapon pool (same content on client and server). Null if unset. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	UFPSRLoadoutPoolDataAsset* GetLoadoutPool() const;

	/** This player's current loadout pick (null until chosen). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	UFPSRWeaponDataAsset* GetSelectedWeapon() const;

	/** True if the local player is currently ready (drives the ready-button visual state). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	bool IsLocalPlayerReady() const;

	/** True if the local player is the host (listen server / standalone). UI hint (e.g. a "host" tag); the run
	 *  start is no longer host-gated (U11a — all-ready starts it). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	bool IsLocalPlayerHost() const;

	/** This lobby's join code (6 chars). Empty until a session exists / before it replicates to a client. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	FString GetLobbyCode() const;

	/** Fired when the local player's loadout selection changes — the WBP refreshes its highlight/state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Lobby")
	void OnLoadoutRefreshed();

	/** Fired when the local player's ready state changes — the WBP refreshes its ready-button visual. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Lobby")
	void OnReadyRefreshed();

private:
	/** Bind to the local PlayerState's OnLoadoutChanged; retries until the PlayerState exists (client replication). */
	bool TryBindPlayerState();

	UFUNCTION()
	void HandleLoadoutChanged();

	UFUNCTION()
	void HandleReadyChanged();

	/** Retry timer for the PlayerState binding. */
	FTimerHandle BindRetryTimer;

	/** Whether OnLoadoutChanged is currently bound (so destruct can unbind cleanly). */
	bool bBoundToPlayerState = false;
};
