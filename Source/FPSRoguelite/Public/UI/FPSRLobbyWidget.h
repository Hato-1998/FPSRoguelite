// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "FPSRLobbyWidget.generated.h"

class UFPSRLoadoutPoolDataAsset;
class UFPSRWeaponDataAsset;

/** One row of the lobby player list — precomputed server-replicated state so the WBP can ForEach this array
 *  directly (no PlayerState cast / null-deref in the graph). Built by UFPSRLobbyWidget::GetLobbyPlayerRows. */
USTRUCT(BlueprintType)
struct FFPSRLobbyPlayerRow
{
	GENERATED_BODY()

	/** Player display name. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Lobby")
	FString PlayerName;

	/** Chosen loadout weapon's display name (empty when bHasWeapon is false). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Lobby")
	FText WeaponName;

	/** True once this player has picked a loadout weapon (false => still choosing). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Lobby")
	bool bHasWeapon = false;

	/** True when this player is ready. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Lobby")
	bool bReady = false;

	/** True for the local player's own row (so the UI can highlight self). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Lobby")
	bool bIsLocalPlayer = false;
};

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

	/** Provide a focus target so CommonUI focuses the lobby (not the game viewport) when it activates. Without this,
	 *  CommonUI logs "No focus target ... focusing the game viewport" and keyboard/gamepad + CommonUI input actions
	 *  (Ready/Invite/Join) are dead in the lobby (mouse still works via hit-testing). Returns the first interactive
	 *  button found in the widget tree. (The other CommonUI menus share this latent gap; the lobby is fixed first
	 *  because it is keyboard-action driven.) */
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

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

	/** Seconds left on the all-ready start countdown (0 when not counting down). Reads the replicated GameState
	 *  value, so it works on host AND remote clients (the lobby GameMode timer is server-only). (U11a) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	float GetReadyCountdownRemaining() const;

	/** Server-replicated lobby roster (name / chosen weapon / ready / is-self), one row per participant. The WBP
	 *  ForEach-es this directly to build the player list — no PlayerState cast or null-guard needed in the graph. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	TArray<FFPSRLobbyPlayerRow> GetLobbyPlayerRows() const;

	/** The full player roster preformatted as a single multi-line FText (one row per participant:
	 *  self-marker / name / chosen weapon / ready). The WBP binds this straight to a TextBlock — no
	 *  loop or per-field formatting in the graph (ForEach macros aren't authorable via the tooling). (U11a) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	FText GetLobbyPlayerListText() const;

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
