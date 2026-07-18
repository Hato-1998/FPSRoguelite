// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Card/FPSRCardTypes.h"
#include "Hero/FPSRFeedbackTypes.h"
#include "Core/FPSRGameFlowTypes.h"
#include "FPSRPlayerController.generated.h"

class UInputMappingContext;
class UFPSRPrimaryGameLayout;
class UFPSRCardSelectWidget;
class UCommonActivatableWidget;
class UFPSRGameHUDWidget;
class UFPSRCardDataAsset;
class UFPSRResultWidget;

/** Base player controller. Adds the default Enhanced Input mapping context for the local player and
 *  drives the card-selection UI flow over server-authoritative RPCs.
 *
 *  Security: the server caches the offer it issued (per-PC) and applies only from that cache by index —
 *  the client sends an index, never a card/magnitude. The offer type (opening / level-up / mission reward)
 *  is server state, not a client-supplied flag. Selection drives the global freeze via GameState (§2-2). */
UCLASS()
class FPSROGUELITE_API AFPSRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRPlayerController();

	virtual void BeginPlay() override;

	/** Add the local player's default Enhanced Input mapping context (idempotent — removes a prior copy first).
	 *  Public so the possessed pawn's input setup can also trigger it: the pawn's SetupPlayerInputComponent is the
	 *  one hook guaranteed to run after a travel possession (the swapped gameplay PC's own SetupInputComponent does
	 *  NOT re-run, leaving Enhanced Input dead). Caller is logged for diagnostics. (P7 §3-4) */
	void ApplyDefaultMappingContext(const TCHAR* Caller);

	/** Server-only (NOT an RPC): begin an opening-seed sequence of Count picks. Trusted server / authority
	 *  debug only — deliberately not client-callable so a client can't grant itself free offers. */
	void BeginOpeningSeed(int32 Count);

	/** Server: grant one weapon-unlock pick (mission clear / level milestone). */
	void GrantWeaponUnlock();

#if !UE_BUILD_SHIPPING
	/** Debug (authority/host): fill the local player's current weapon to its fragment slot cap from the weapon's
	 *  UnlockableFeatures, so the at-cap replacement flow can be exercised in PIE (FPSR.Frag.Fill). */
	void DebugFillFragmentSlots();

	/** Debug (authority/host): pick the fragment card in the current cached offer, dropping the equipped fragment at
	 *  DropIndex via the same server-authoritative path as the replacement RPC (FPSR.Frag.Replace). An out-of-range
	 *  DropIndex demonstrates the anti-cheat rejection. */
	void DebugSelectFragmentReplacement(int32 DropIndex);
#endif

	/** Client intent: notify the server the local UI is ready, so the server can issue the one-time
	 *  run-start opening seed (§2-2). Count is fixed server-side; processed at most once per player. */
	UFUNCTION(Server, Reliable)
	void ServerNotifyClientReady();

	/** Client intent (U P-F): acknowledge the current flow-field topology generation to the server, so the allocator /
	 *  swarm stop gating this player out. Sent from BeginPlay (initial) and from GameState::OnRep_TopologyGeneration
	 *  whenever it changes. Applied to the owning PlayerState (monotone); buffered if the server-side PlayerState isn't
	 *  linked yet. Authority-gated; a forged early/high value only ever advances THIS player's own ack. */
	UFUNCTION(Server, Reliable)
	void ServerAckTopology(int32 Gen);

	/** Server-only (NOT an RPC): if no offer is currently shown, present the next one this player needs
	 *  (priority: opening seed > mission reward > level-up). Drives the freeze-time card flow. */
	void PresentNextOfferIfNeeded();

	/** Server-only: whether this player still owes any card selection (used by GameState freeze logic). */
	bool HasPendingSelection() const;

	/** Server-only: whether an offer is currently cached/presented (don't replace a mid-selection offer). */
	bool HasActiveOffer() const { return CachedOffer.Num() > 0; }

	/** Server-only: pull back an offer this player can no longer act on (they went DBNO/Dead). RefreshPauseState
	 *  skips non-Alive players, so without this their modal would linger over live gameplay and they could still
	 *  accept the stale offer. The PICK is not consumed — clearing the cache lets PresentNextOfferIfNeeded re-draw
	 *  and re-present it on revive, so nothing is lost. Also closes the stale-selection window: HandleCardSelection
	 *  validates the index against CachedOffer, which is now empty. */
	void WithdrawActiveOffer();

	/** Server-only: whether this player's run-start opening seed has been issued (used by the director's
	 *  pre-combat hold so spawning waits until the opening-seed flow has at least begun). */
	bool HasStartedOpeningSeed() const { return bOpeningSeedIssued; }

	/** Client (owner): present the server-issued offer (tagged with its OfferId) in the card-select modal. */
	UFUNCTION(Client, Reliable)
	void ClientPresentCards(int32 OfferId, const TArray<FFPSRCardDraw>& Offer);

	/** Server RPC (client intent): apply the cached offer entry at Index. OfferId must match the current
	 *  server offer (stale/duplicate selections from spam/double-click are ignored). */
	UFUNCTION(Server, Reliable)
	void ServerSelectCard(int32 Index, int32 OfferId);

	/** Server RPC (client intent): apply the cached offer entry at Index, but when its behavior fragment would
	 *  exceed the target weapon's slot cap, DROP the equipped fragment at ReplaceFragmentIndex first (U6 swap).
	 *  OfferId must match; the drop index is validated server-side against the resolved target weapon's distinct
	 *  fragment list (a forged/out-of-range index is rejected — the client only ever sends an index intent). */
	UFUNCTION(Server, Reliable)
	void ServerSelectCardReplacement(int32 Index, int32 OfferId, int32 ReplaceFragmentIndex);

	/** Server RPC (client intent): consume a reroll charge and redraw the current offer (level-up / opening
	 *  only; mission-reward offers are single-card and not rerollable). OfferId must match the shown offer. */
	UFUNCTION(Server, Reliable)
	void ServerRerollOffer(int32 OfferId);

	/** Client (owner): remove the card-select modal. */
	UFUNCTION(Client, Reliable)
	void ClientDismissCardUI();

	/** Client (owner): server-confirmed hit marker (Hit / Crit / Kill) — forwards to the local pawn's feedback
	 *  component. Unreliable: cosmetic, high-frequency during sustained fire, and safe to drop under packet loss
	 *  (must not back up the reliable channel ahead of gameplay RPCs). (Game.MD §2-14) */
	UFUNCTION(Client, Unreliable)
	void ClientNotifyHitMarker(EFPSRHitMarkerType MarkerType);

	/** Client (owner): incoming damage came from InstigatorLocation — forwards to the local feedback component,
	 *  which converts it to a camera-relative angle for the damage-direction indicator. Unreliable cosmetic. */
	UFUNCTION(Client, Unreliable)
	void ClientNotifyDamageFrom(FVector InstigatorLocation);

	/** Client (owner): ranged enemy SourceId began/ended targeting this player from SourceLocation (§2-6
	 *  pre-warning) — forwards to the local feedback component (multi-source, keyed by SourceId). Reliable: a
	 *  low-frequency on/off state where a dropped "off" would leave a warning stuck on screen. */
	UFUNCTION(Client, Reliable)
	void ClientNotifyRangedTarget(int32 SourceId, FVector SourceLocation, bool bActive);

	/** Server: the owner client could not present the offer (no layout/widget class/modal layer) — release
	 *  this player's outstanding picks so the global freeze can't hard-lock on a broken UI. OfferId must
	 *  match the current offer (prevents a client discarding an unfavorable offer for free). */
	UFUNCTION(Server, Reliable)
	void ServerAbandonOffer(int32 OfferId);

	/** Client (owner): show the run outcome widget (called when AFPSRGameMode::EndRun fires). */
	UFUNCTION(Client, Reliable)
	void ClientShowRunResult(EFPSRRunOutcome Outcome);

	/** Client (owner): commit THIS player's local meta save at run end (U10). Server-authoritative EndRun signals
	 *  each client to persist its OWN save — the server never touches a player's save (per-player local ownership).
	 *  Reliable + issued at EndRun (~PostRunTravelDelay before the post-run lobby ServerTravel) so it is delivered and
	 *  starts the local async save before the client travels; the GameInstance SaveManager carries it to completion
	 *  across the travel. At U10 this persists the versioned scaffold; P0-③ folds run rewards into the save first. */
	UFUNCTION(Client, Reliable)
	void ClientCommitMetaSave(EFPSRRunOutcome Outcome);

	/** Server (host/authority): return to the lobby hub now (non-authority result widget path). Mirrors the GameMode's
	 *  automatic post-run lobby travel but fires immediately on the player's Return click instead of after the delay
	 *  (P7 §3-6 — all runs return to the lobby, not the main menu). */
	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToLobby();

	/** Local (owner): open the settings overlay on the GameMenu layer (idempotent — won't push a second copy).
	 *  Non-pause: 4-player coop never stops the server, so the run keeps running underneath (SoundSettings
	 *  handoff "인게임 논-포즈"). Bound to IA_Menu (Esc) by the possessed pawn. CommonUI Back / the widget's Back
	 *  button pops it. */
	void OpenSettingsOverlay();

protected:
	virtual void SetupInputComponent() override;

	/** The mapping context must be (re)applied after lobby->gameplay travel — the swapped-in gameplay PC does NOT
	 *  re-run SetupInputComponent, so its context never lands and Enhanced Input is dead. No single possession
	 *  hook fires for every host/client travel case, so ApplyDefaultMappingContext() is called (idempotently) from
	 *  OnPossess (server / listen-host authority), AcknowledgePossession (owning client), and the pawn's input
	 *  setup. (P7 §3-4) */
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;

	/** U (P-F): apply any topology ack buffered before the PlayerState was linked. InitPlayerState covers the server
	 *  (where ServerAckTopology may land before the PS is set); OnRep_PlayerState covers the owning-client PS arrival. */
	virtual void InitPlayerState() override;
	virtual void OnRep_PlayerState() override;

	/** Server-only: draw + cache an offer of the given type for this player and present it. */
	void RequestCardOffer(EFPSROfferType OfferType);

	/** Create the local-player layout root on demand (idempotent). Returns true if it now exists. */
	bool EnsurePrimaryLayout();

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRPrimaryGameLayout> PrimaryLayoutClass;

	/** Game-layer HUD container pushed for the local player (holds run-state HUD, hit marker, indicators). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRGameHUDWidget> GameHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRCardSelectWidget> CardSelectWidgetClass;

	/** Result widget (Victory/Defeat screen) shown when a run ends. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRResultWidget> ResultWidgetClass;

	/** Settings overlay (master volume) pushed to the GameMenu layer by IA_Menu in-game. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UCommonActivatableWidget> SettingsWidgetClass;

private:
	/** Server: refresh the global freeze + present after a selection/grant changed this player's pending picks. */
	void NotifyPauseStateDirty();

	/** Server: shared body of ServerSelectCard / ServerSelectCardReplacement — validate the offer (OfferId + index),
	 *  apply the cached entry (passing ReplaceFragmentIndex for the at-cap fragment swap; INDEX_NONE = plain pick),
	 *  then do per-type bookkeeping + re-present. Returns true if the selection was accepted (offer advanced). */
	bool HandleCardSelection(int32 Index, int32 OfferId, int32 ReplaceFragmentIndex);

	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;

	/** Currently shown card-select modal instance (owner client). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRCardSelectWidget> ActiveCardWidget;

	/** Currently shown settings overlay instance (owner client) — guards against a second push. */
	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidget> ActiveSettingsWidget;

	/** Server-only: the offer last issued to this player; selection applies from here by index. */
	TArray<FFPSRCardDraw> CachedOffer;

	/** Server-only: what the cached offer represents (drives consume behavior + reroll eligibility). */
	EFPSROfferType CurrentOfferType = EFPSROfferType::LevelUp;

	/** Server-only: remaining opening-seed picks to present. */
	int32 PendingOpeningSeeds = 0;

	/** Server-only: monotonic id of the current offer; selections must echo it (anti double-apply). */
	int32 CurrentOfferId = 0;

	/** Server-only: the run-start opening seed has been issued for this player (one-time guard). */
	bool bOpeningSeedIssued = false;

	/** Server-only (U P-F): apply PendingAckTopologyGeneration to the PlayerState once it is linked (called from
	 *  InitPlayerState / OnRep_PlayerState). No-op if nothing is buffered. */
	void ApplyPendingTopologyAck();

	/** Server-only (U P-F): a topology ack that arrived before the PlayerState was linked (-1 = none). Applied on link. */
	int32 PendingAckTopologyGeneration = -1;
};
