// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRPlayerController.generated.h"

class UInputMappingContext;
class UFPSRPrimaryGameLayout;
class UFPSRCardSelectWidget;
class UCommonActivatableWidget;

/** Base player controller. Adds the default Enhanced Input mapping context for the local player and
 *  drives the card-selection UI flow over server-authoritative RPCs.
 *
 *  Security: the server caches the offer it issued (per-PC) and applies only from that cache by index —
 *  the client sends an index, never a card/magnitude. The consume mode (level-up vs opening seed) is
 *  also server state, not a client-supplied flag. */
UCLASS()
class FPSROGUELITE_API AFPSRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRPlayerController();

	virtual void BeginPlay() override;

	/** Server-only (NOT an RPC): begin an opening-seed sequence of Count picks (applied without
	 *  consuming the level-up stack). Initiated by trusted server logic / authority-holding debug only —
	 *  deliberately not client-callable so a client cannot grant itself free non-consuming offers. */
	void BeginOpeningSeed(int32 Count);

	/** Client intent: notify the server that this player's local UI is ready, so the server can issue the
	 *  one-time run-start opening seed (§2-2). The COUNT is fixed server-side (BeginOpeningSeed(2)); the
	 *  client cannot choose it, and the server processes this at most once per player (re-sends ignored). */
	UFUNCTION(Server, Reliable)
	void ServerNotifyClientReady();

	/** Server-only (NOT an RPC): draw + cache an offer for this player and present it. The consume mode
	 *  is server state, never a client-supplied argument (server authority over progression). */
	void RequestCardOffer(bool bConsumeLevelUp);

	/** Server-only: whether an offer is currently cached/presented (don't replace a mid-selection offer). */
	bool HasActiveOffer() const { return CachedOffer.Num() > 0; }

	/** Server-only: whether this player's run-start opening seed has been issued (started). */
	bool HasStartedOpeningSeed() const { return bOpeningSeedIssued; }

	/** Server-only: whether this player's opening-seed selection is finished (all picks made, or
	 *  none required / released). The run director holds combat spawning until every player is complete. */
	bool IsOpeningSeedComplete() const { return bOpeningSeedComplete; }

	/** Client (owner): present the server-issued offer (tagged with its OfferId) in the card-select modal. */
	UFUNCTION(Client, Reliable)
	void ClientPresentCards(int32 OfferId, const TArray<FFPSRCardDraw>& Offer);

	/** Server RPC (client intent): apply the cached offer entry at Index. OfferId must match the current
	 *  server offer (stale/duplicate selections from spam/double-click are ignored). Applies only from the
	 *  server's cached offer with the server-stored consume mode. */
	UFUNCTION(Server, Reliable)
	void ServerSelectCard(int32 Index, int32 OfferId);

	/** Server RPC (client intent): consume a reroll charge and redraw the current offer (same mode).
	 *  OfferId must match the shown offer (a stale reroll from a previous offer is ignored). */
	UFUNCTION(Server, Reliable)
	void ServerRerollOffer(int32 OfferId);

	/** Client (owner): remove the card-select modal. */
	UFUNCTION(Client, Reliable)
	void ClientDismissCardUI();

	/** Server: the owner client could not present the offer (no layout/widget class/modal layer) — release
	 *  the cached offer so the player isn't stranded with a pending pick and no UI (re-presentable later).
	 *  OfferId must match the current offer (prevents a client discarding an unfavorable offer for free). */
	UFUNCTION(Server, Reliable)
	void ServerAbandonOffer(int32 OfferId);

protected:
	virtual void SetupInputComponent() override;

	/** Create the local-player layout root on demand (idempotent). Returns true if it now exists. */
	bool EnsurePrimaryLayout();

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRPrimaryGameLayout> PrimaryLayoutClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UCommonActivatableWidget> XPBarWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRCardSelectWidget> CardSelectWidgetClass;

private:
	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;

	/** Currently shown card-select modal instance (owner client). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRCardSelectWidget> ActiveCardWidget;

	/** Server-only: the offer last issued to this player; selection applies from here by index. */
	TArray<FFPSRCardDraw> CachedOffer;

	/** Server-only: whether the cached offer's selection consumes a level-up pick. */
	bool bOfferConsumesLevelUp = false;

	/** Server-only: remaining opening-seed picks to present. */
	int32 PendingOpeningSeeds = 0;

	/** Server-only: monotonic id of the current offer; selections must echo it (anti double-apply). */
	int32 CurrentOfferId = 0;

	/** Server-only: the run-start opening seed has been issued for this player (one-time guard). */
	bool bOpeningSeedIssued = false;

	/** Server-only: this player's opening-seed selection is finished (all picks made, or released/none).
	 *  Drives the run director's pre-combat hold so spawns don't start mid-selection (§2-2). */
	bool bOpeningSeedComplete = false;
};
