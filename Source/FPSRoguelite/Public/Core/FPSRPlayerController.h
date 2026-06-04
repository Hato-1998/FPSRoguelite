// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRPlayerController.generated.h"

class UInputMappingContext;
class UFPSRPrimaryGameLayout;
class UFPSRCardSelectWidget;
class UCommonActivatableWidget;
class UFPSRCardDataAsset;

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

	/** Server-only (NOT an RPC): begin an opening-seed sequence of Count picks. Trusted server / authority
	 *  debug only — deliberately not client-callable so a client can't grant itself free offers. */
	void BeginOpeningSeed(int32 Count);

	/** Server-only (NOT an RPC): grant this player one mission-reward pick for the given reward card. The
	 *  reward is presented (and the run freezes) on the next RefreshPauseState. */
	void GrantMissionReward(UFPSRCardDataAsset* RewardCard);

	/** Client intent: notify the server the local UI is ready, so the server can issue the one-time
	 *  run-start opening seed (§2-2). Count is fixed server-side; processed at most once per player. */
	UFUNCTION(Server, Reliable)
	void ServerNotifyClientReady();

	/** Server-only (NOT an RPC): if no offer is currently shown, present the next one this player needs
	 *  (priority: opening seed > mission reward > level-up). Drives the freeze-time card flow. */
	void PresentNextOfferIfNeeded();

	/** Server-only: whether this player still owes any card selection (used by GameState freeze logic). */
	bool HasPendingSelection() const;

	/** Server-only: whether an offer is currently cached/presented (don't replace a mid-selection offer). */
	bool HasActiveOffer() const { return CachedOffer.Num() > 0; }

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

	/** Server RPC (client intent): consume a reroll charge and redraw the current offer (level-up / opening
	 *  only; mission-reward offers are single-card and not rerollable). OfferId must match the shown offer. */
	UFUNCTION(Server, Reliable)
	void ServerRerollOffer(int32 OfferId);

	/** Client (owner): remove the card-select modal. */
	UFUNCTION(Client, Reliable)
	void ClientDismissCardUI();

	/** Server: the owner client could not present the offer (no layout/widget class/modal layer) — release
	 *  this player's outstanding picks so the global freeze can't hard-lock on a broken UI. OfferId must
	 *  match the current offer (prevents a client discarding an unfavorable offer for free). */
	UFUNCTION(Server, Reliable)
	void ServerAbandonOffer(int32 OfferId);

protected:
	virtual void SetupInputComponent() override;

	/** Server-only: draw + cache an offer of the given type for this player and present it. */
	void RequestCardOffer(EFPSROfferType OfferType);

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
	/** Server: refresh the global freeze + present after a selection/grant changed this player's pending picks. */
	void NotifyPauseStateDirty();

	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;

	/** Currently shown card-select modal instance (owner client). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRCardSelectWidget> ActiveCardWidget;

	/** Server-only: the offer last issued to this player; selection applies from here by index. */
	TArray<FFPSRCardDraw> CachedOffer;

	/** Server-only: what the cached offer represents (drives consume behavior + reroll eligibility). */
	EFPSROfferType CurrentOfferType = EFPSROfferType::LevelUp;

	/** Server-only: remaining opening-seed picks to present. */
	int32 PendingOpeningSeeds = 0;

	/** Server-only: queued mission-reward cards (one per pending mission-reward pick), FIFO. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UFPSRCardDataAsset>> PendingMissionRewardCards;

	/** Server-only: monotonic id of the current offer; selections must echo it (anti double-apply). */
	int32 CurrentOfferId = 0;

	/** Server-only: the run-start opening seed has been issued for this player (one-time guard). */
	bool bOpeningSeedIssued = false;
};
