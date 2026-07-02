// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputMappingContext.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRFlowLog.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRGameMode.h"
#include "Core/FPSRGameFlowSubsystem.h"
#include "MetaProgression/FPSRSaveGameSubsystem.h"
#include "Engine/GameInstance.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRCardSelectWidget.h"
#include "UI/FPSRGameHUDWidget.h"
#include "UI/FPSRResultWidget.h"
#include "Hero/FPSRPlayerFeedbackComponent.h"
#include "GameFramework/Pawn.h"

AFPSRPlayerController::AFPSRPlayerController()
{
}

void AFPSRPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		EnsurePrimaryLayout();
		// Tell the server the local UI is up so it can issue the run-start opening seed. If the layout
		// wasn't ready, ClientPresentCards lazily rebuilds it; the offer flow's abandon/retry covers the rest.
		ServerNotifyClientReady();
	}
}

void AFPSRPlayerController::ServerNotifyClientReady_Implementation()
{
	if (!HasAuthority() || bOpeningSeedIssued)
	{
		return;
	}
	BeginOpeningSeed(2);
}

bool AFPSRPlayerController::EnsurePrimaryLayout()
{
	if (PrimaryLayout)
	{
		return true;
	}

	if (!IsLocalController() || !PrimaryLayoutClass)
	{
		return false;
	}

	PrimaryLayout = CreateWidget<UFPSRPrimaryGameLayout>(this, PrimaryLayoutClass);
	if (!PrimaryLayout)
	{
		UE_LOG(LogFPSR, Error, TEXT("[UI] Failed to create PrimaryGameLayout"));
		return false;
	}

	PrimaryLayout->AddToViewport();

	if (GameHUDWidgetClass)
	{
		PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Game")), GameHUDWidgetClass);
	}

	return true;
}

void AFPSRPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	ApplyDefaultMappingContext(TEXT("SetupInputComponent"));
}

void AFPSRPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// Server / listen-server host (authority) possession. After lobby->gameplay travel the swapped-in gameplay PC
	// does NOT re-run SetupInputComponent, so re-apply the mapping context on possession. (AcknowledgePossession
	// covers the remote-client case.)
	ApplyDefaultMappingContext(TEXT("OnPossess"));
}

void AFPSRPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	ApplyDefaultMappingContext(TEXT("AcknowledgePossession"));
}

void AFPSRPlayerController::ApplyDefaultMappingContext(const TCHAR* Caller)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Input] (%s) EnhancedInputLocalPlayerSubsystem not found (local player not ready yet)"), Caller);
		return;
	}

	if (!DefaultMappingContext)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Input] (%s) DefaultMappingContext is NULL (assign it in BP_FPSRPlayerController)"), Caller);
		return;
	}

	// Idempotent: several possession/input hooks call this (none fire for every host/client travel case), so clear
	// a prior copy before re-adding so the contexts never stack duplicates.
	Subsystem->RemoveMappingContext(DefaultMappingContext);
	Subsystem->AddMappingContext(DefaultMappingContext, 0);
	UE_LOG(LogFPSR, Verbose, TEXT("[Input] (%s) Added DefaultMappingContext to local player"), Caller);
}

void AFPSRPlayerController::BeginOpeningSeed(int32 Count)
{
	if (!HasAuthority())
	{
		return;
	}

	bOpeningSeedIssued = true;
	PendingOpeningSeeds = FMath::Max(0, Count);
	// Freezes the run and presents the first opening card (if any) via RefreshPauseState.
	NotifyPauseStateDirty();
}

void AFPSRPlayerController::GrantWeaponUnlock()
{
	if (!HasAuthority())
	{
		return;
	}
	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		// Dead players don't participate in card/weapon-unlock selection (mission-clear grant skips them).
		if (!PS->IsAlive())
		{
			return;
		}
		PS->AddWeaponUnlockPick();
	}
}

bool AFPSRPlayerController::HasPendingSelection() const
{
	if (CachedOffer.Num() > 0 || PendingOpeningSeeds > 0)
	{
		return true;
	}
	if (const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		return PS->GetWeaponUnlockPicksPending() > 0 || PS->GetCardPicksPending() > 0;
	}
	return false;
}

void AFPSRPlayerController::PresentNextOfferIfNeeded()
{
	if (!HasAuthority() || CachedOffer.Num() > 0)
	{
		return; // an offer is already shown; don't replace a mid-selection offer
	}

	if (PendingOpeningSeeds > 0)
	{
		RequestCardOffer(EFPSROfferType::OpeningSeed);
		return;
	}

	const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (PS && PS->GetWeaponUnlockPicksPending() > 0)
	{
		RequestCardOffer(EFPSROfferType::WeaponUnlock);
		return;
	}
	if (PS && PS->GetCardPicksPending() > 0)
	{
		RequestCardOffer(EFPSROfferType::LevelUp);
		return;
	}

	// Nothing left for this player — make sure no modal lingers.
	ClientDismissCardUI();
}

void AFPSRPlayerController::NotifyPauseStateDirty()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->RefreshPauseState();
	}
	else
	{
		PresentNextOfferIfNeeded();
	}
}

void AFPSRPlayerController::RequestCardOffer(EFPSROfferType OfferType)
{
	if (!HasAuthority())
	{
		return;
	}

	UFPSRCardSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSRCardSubsystem>() : nullptr;
	if (!Sub)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Card] CardSubsystem not found on server"));
		return;
	}

	CurrentOfferType = OfferType;

	if (OfferType == EFPSROfferType::WeaponUnlock)
	{
		CachedOffer = Sub->DrawWeaponUnlockOffer(this, 3);
	}
	else
	{
		CachedOffer = Sub->DrawCards(this);
	}

	if (CachedOffer.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Card] Empty offer for type %d (check pool)"), (int32)OfferType);
		// Can't present this selection — release it so the global freeze doesn't hard-lock on this player.
		if (OfferType == EFPSROfferType::OpeningSeed)
		{
			PendingOpeningSeeds = 0;
		}
		else if (OfferType == EFPSROfferType::WeaponUnlock)
		{
			if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>()) { PS->ConsumeWeaponUnlockPick(); }
		}
		else if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
		{
			PS->ConsumeCardPick();
		}
		ClientDismissCardUI();
		NotifyPauseStateDirty();
		return;
	}

	++CurrentOfferId;
	ClientPresentCards(CurrentOfferId, CachedOffer);
}

void AFPSRPlayerController::ClientPresentCards_Implementation(int32 OfferId, const TArray<FFPSRCardDraw>& Offer)
{
	if (!IsLocalController())
	{
		return;
	}

	// Lazily build the layout (covers an offer arriving before BeginPlay finished). If we still can't
	// present, tell the server to release the offer so the pending pick isn't stranded behind it.
	if (!EnsurePrimaryLayout() || !CardSelectWidgetClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[UI] Cannot present card offer (PrimaryLayout/CardSelectWidgetClass missing) — abandoning offer"));
		ServerAbandonOffer(OfferId);
		return;
	}

	if (!ActiveCardWidget)
	{
		UCommonActivatableWidget* Pushed =
			PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Modal")), CardSelectWidgetClass);
		ActiveCardWidget = Cast<UFPSRCardSelectWidget>(Pushed);
	}

	// Push can fail if the Modal layer stack isn't registered (e.g. wrong BindWidget name in WBP). Don't
	// leave the server's offer stranded behind HasActiveOffer() — release it so it can be re-presented.
	if (!ActiveCardWidget)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[UI] Modal layer push failed (check WBP_PrimaryGameLayout 'Layer_Modal' stack) — abandoning offer"));
		ServerAbandonOffer(OfferId);
		return;
	}

	ActiveCardWidget->SetCardOffers(OfferId, Offer);
}

void AFPSRPlayerController::ServerSelectCard_Implementation(int32 Index, int32 OfferId)
{
	HandleCardSelection(Index, OfferId, INDEX_NONE);
}

void AFPSRPlayerController::ServerSelectCardReplacement_Implementation(int32 Index, int32 OfferId, int32 ReplaceFragmentIndex)
{
	HandleCardSelection(Index, OfferId, ReplaceFragmentIndex);
}

bool AFPSRPlayerController::HandleCardSelection(int32 Index, int32 OfferId, int32 ReplaceFragmentIndex)
{
	// Ignore stale/duplicate selections (double-click, spam) that don't match the current offer.
	if (OfferId != CurrentOfferId)
	{
		return false;
	}

	// Ignore invalid intent — keep the current offer up so the client can't force a free redraw.
	if (!CachedOffer.IsValidIndex(Index))
	{
		return false;
	}

	// Apply from the SERVER-cached offer only (client sent an index, never a card). ReplaceFragmentIndex is likewise
	// just an index — the card effect validates it against the resolved target weapon's distinct-fragment list.
	UFPSRCardSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSRCardSubsystem>() : nullptr;
	const bool bApplied = Sub && Sub->ApplyCard(this, CachedOffer[Index], CurrentOfferType, ReplaceFragmentIndex);
	if (!bApplied)
	{
		// Rejected (no pending pick, or at slot cap with no valid replacement index) — leave the offer up; no advance.
		return false;
	}

	// Per-type bookkeeping (the level-up / weapon-unlock pick was consumed inside ApplyCard).
	if (CurrentOfferType == EFPSROfferType::OpeningSeed)
	{
		if (PendingOpeningSeeds > 0) { --PendingOpeningSeeds; }
	}

	CachedOffer.Reset();

	// Re-present this player's next pick (if any) and recompute the global freeze (unpause when all done).
	NotifyPauseStateDirty();
	return true;
}

void AFPSRPlayerController::ServerRerollOffer_Implementation(int32 OfferId)
{
	// Ignore a stale reroll from a previous offer (queued/double-click after the next offer was issued).
	if (OfferId != CurrentOfferId)
	{
		return;
	}

	// Reroll only applies to an actively presented offer — never a free redraw outside the flow.
	if (CachedOffer.Num() == 0)
	{
		return;
	}

	// WeaponUnlock offers are fixed unlock choices — not rerollable.
	if (CurrentOfferType == EFPSROfferType::WeaponUnlock)
	{
		return;
	}

	UFPSRCardSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSRCardSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	if (!Sub->TryReroll(this))
	{
		UE_LOG(LogFPSR, Verbose, TEXT("[Card] Reroll denied (no charges)"));
		return;
	}

	CachedOffer = Sub->DrawCards(this);
	if (CachedOffer.Num() == 0)
	{
		ClientDismissCardUI();
		NotifyPauseStateDirty();
		return;
	}

	++CurrentOfferId;
	ClientPresentCards(CurrentOfferId, CachedOffer);
}

void AFPSRPlayerController::ClientDismissCardUI_Implementation()
{
	if (ActiveCardWidget && PrimaryLayout)
	{
		PrimaryLayout->RemoveWidgetFromLayer(ActiveCardWidget);
	}
	ActiveCardWidget = nullptr;
}

void AFPSRPlayerController::OpenSettingsOverlay()
{
	if (!IsLocalController())
	{
		return; // UI + local audio settings are owner-local only (Codex plan gate: local-controller guard)
	}
	if (ActiveSettingsWidget)
	{
		return; // already open — don't push a second copy (CommonUI Back pops the existing one)
	}
	if (!EnsurePrimaryLayout() || !SettingsWidgetClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[UI] Cannot open settings overlay (PrimaryLayout/SettingsWidgetClass missing)"));
		return;
	}

	// GameMenu layer = non-pause overlay above the HUD. The run keeps running (4-player coop, §2-2 freeze is
	// card-select only and unrelated).
	UCommonActivatableWidget* Pushed =
		PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.GameMenu")), SettingsWidgetClass);
	ActiveSettingsWidget = Pushed;

	if (Pushed)
	{
		// Clear the guard when the widget pops itself (Back button / CommonUI Back action) so it can reopen.
		Pushed->OnDeactivated().AddWeakLambda(this, [this]() { ActiveSettingsWidget = nullptr; });
	}
}

void AFPSRPlayerController::ClientNotifyHitMarker_Implementation(EFPSRHitMarkerType MarkerType)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFPSRPlayerFeedbackComponent* Feedback = ControlledPawn->FindComponentByClass<UFPSRPlayerFeedbackComponent>())
		{
			Feedback->NotifyHitConfirmed(MarkerType);
		}
	}
}

void AFPSRPlayerController::ClientNotifyDamageFrom_Implementation(FVector InstigatorLocation)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFPSRPlayerFeedbackComponent* Feedback = ControlledPawn->FindComponentByClass<UFPSRPlayerFeedbackComponent>())
		{
			Feedback->ReceiveDamageFromWorld(InstigatorLocation);
		}
	}
}

void AFPSRPlayerController::ClientNotifyRangedTarget_Implementation(int32 SourceId, FVector SourceLocation, bool bActive)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UFPSRPlayerFeedbackComponent* Feedback = ControlledPawn->FindComponentByClass<UFPSRPlayerFeedbackComponent>())
		{
			Feedback->ReceiveRangedTarget(SourceId, SourceLocation, bActive);
		}
	}
}

void AFPSRPlayerController::ServerAbandonOffer_Implementation(int32 OfferId)
{
	// Only the offer the server actually sent can be abandoned (stale/forged ids ignored), so a client
	// can't discard an unfavorable offer for free outside the present-failure path.
	if (OfferId != CurrentOfferId)
	{
		return;
	}

	// Present failure (broken WBP) — release this player's outstanding picks so the GLOBAL freeze can't
	// hard-lock everyone behind a UI that can't show. Loud log: this is a content-setup error, not normal.
	UE_LOG(LogFPSR, Error, TEXT("[UI] Card offer abandoned — releasing this player's pending picks (check WBP setup)"));
	CachedOffer.Reset();
	PendingOpeningSeeds = 0;
	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		while (PS->ConsumeCardPick()) {}
		while (PS->ConsumeWeaponUnlockPick()) {}
	}

	NotifyPauseStateDirty();
}

void AFPSRPlayerController::ClientShowRunResult_Implementation(EFPSRRunOutcome Outcome)
{
	const TCHAR* OutcomeStr = Outcome == EFPSRRunOutcome::Victory ? TEXT("Victory")
		: (Outcome == EFPSRRunOutcome::Defeat ? TEXT("Defeat") : TEXT("None"));
	// Diagnostic (B2): confirms the per-client RPC actually arrived. If a client never shows the result screen and
	// no "RESULT" line appears in its FlowLog, the EndRun loop / RPC didn't reach it; if it appears but the next
	// line reports a missing PrimaryLayout/ResultWidgetClass, the gameplay PlayerController BP needs ResultWidgetClass
	// assigned (the most common cause — the RPC + wiring are correct, the content slot is empty).
	FPSRFlowLog::Event(this, TEXT("RESULT"), FString::Printf(TEXT("ClientShowRunResult arrived (outcome=%s)"), OutcomeStr));

	if (!IsLocalController())
	{
		return;
	}

	// Lazily create the layout if needed (usually already exists from BeginPlay).
	if (!EnsurePrimaryLayout() || !ResultWidgetClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[UI] Cannot show run result (PrimaryLayout/ResultWidgetClass missing)"));
		FPSRFlowLog::Event(this, TEXT("RESULT"), FString::Printf(TEXT("ABORT — %s missing"),
			!ResultWidgetClass ? TEXT("ResultWidgetClass (assign on gameplay PC BP)") : TEXT("PrimaryLayout")));
		return;
	}

	UCommonActivatableWidget* Pushed =
		PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Menu")), ResultWidgetClass);

	if (UFPSRResultWidget* ResultWidget = Cast<UFPSRResultWidget>(Pushed))
	{
		ResultWidget->SetOutcome(Outcome);
		FPSRFlowLog::Event(this, TEXT("RESULT"), TEXT("Result widget shown"));
	}
	else
	{
		UE_LOG(LogFPSR, Warning, TEXT("[UI] Failed to push result widget to Menu layer"));
		FPSRFlowLog::Event(this, TEXT("RESULT"), TEXT("ABORT — push to Menu layer failed"));
	}
}

void AFPSRPlayerController::ClientCommitMetaSave_Implementation(EFPSRRunOutcome Outcome)
{
	if (!IsLocalController())
	{
		return;
	}
	// Persist THIS client's local meta save (per-player local ownership — the server issued the signal but never
	// touches our save). At U10 the save holds only the versioned scaffold; P0-③ folds this run's rewards into the
	// SaveManager's save object before this call. All persistence routes through the single SaveManager gateway.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRSaveGameSubsystem* Saves = GI->GetSubsystem<UFPSRSaveGameSubsystem>())
		{
			Saves->RequestSave(EFPSRSaveReason::RunEnd);
		}
	}
}

void AFPSRPlayerController::ServerRequestReturnToMenu_Implementation(EFPSRRunOutcome Outcome)
{
	if (!HasAuthority())
	{
		return;
	}

	// Gate on the run-ended latch so a client can't force a mid-run menu travel (W1 P2-3). This RPC is a P6-A
	// holdover (the result screen now returns to the lobby); the guard hardens the legacy path.
	const AFPSRGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRGameMode>() : nullptr;
	if (!GM || !GM->IsRunEnded())
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRGameFlowSubsystem* Flow = GI->GetSubsystem<UFPSRGameFlowSubsystem>())
		{
			Flow->ReturnToMenu(Outcome);
		}
	}
}

void AFPSRPlayerController::ServerRequestReturnToLobby_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	// The GameMode owns the (single source of truth) lobby travel — fire it now and cancel the pending auto-travel.
	if (AFPSRGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRGameMode>() : nullptr)
	{
		GM->RequestReturnToLobby();
	}
}

#if !UE_BUILD_SHIPPING

void AFPSRPlayerController::DebugFillFragmentSlots()
{
	if (!HasAuthority())
	{
		return;
	}
	APawn* P = GetPawn();
	UFPSRWeaponInventoryComponent* Inv = P ? P->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
	UFPSRWeaponInstance* Instance = Inv ? Inv->GetCurrentInstance() : nullptr;
	UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
	if (!Instance || !Weapon)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Frag] Fill: no current weapon instance"));
		return;
	}

	// Collect this weapon's distinct behavior-fragment cards from its UnlockableFeatures pool (card + fragment paired).
	TArray<UFPSRCardDataAsset*> PoolCards;
	TArray<UFPSRWeaponFragment*> PoolFrags;
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : Weapon->UnlockableFeatures)
	{
		if (!Card)
		{
			continue;
		}
		for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
		{
			const UCardEffect_WeaponBehavior* Beh = Cast<UCardEffect_WeaponBehavior>(Effect);
			if (Beh && Beh->Fragment && !PoolFrags.Contains(Beh->Fragment))
			{
				PoolCards.Add(Card.Get());
				PoolFrags.Add(Beh->Fragment);
			}
		}
	}

	const int32 Cap = Instance->GetMaxFragmentSlots();

	// Reset our known fragments then fill to the cap, so re-running the command is idempotent.
	for (UFPSRWeaponFragment* Frag : PoolFrags)
	{
		Instance->RemoveFragment(Frag);
	}
	int32 Added = 0;
	for (UFPSRWeaponFragment* Frag : PoolFrags)
	{
		if (Added >= Cap)
		{
			break;
		}
		if (Instance->AddFragment(Frag)) { ++Added; }
	}

	TArray<UFPSRWeaponFragment*> Distinct;
	Instance->GetDistinctFragments(Distinct);
	UE_LOG(LogFPSR, Warning, TEXT("[Frag] Fill: '%s' now holds %d/%d distinct fragment(s):"), *Weapon->GetName(), Distinct.Num(), Cap);
	for (int32 i = 0; i < Distinct.Num(); ++i)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Frag]   slot[%d] = %s"), i, Distinct[i] ? *Distinct[i]->GetName() : TEXT("null"));
	}

	// Need at least one MORE distinct fragment than the cap to present an incoming replacement candidate.
	if (PoolCards.Num() <= Cap)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Frag] Fill: weapon '%s' UnlockableFeatures has only %d distinct fragment(s) (need > %d) — cannot present a replacement offer"),
			*Weapon->GetName(), PoolCards.Num(), Cap);
		return;
	}

	// Build + cache a single-card weapon-unlock offer for the first not-yet-equipped fragment, then present it so the
	// run freezes (mirrors the real mission-unlock path). FPSR.Frag.Replace <dropIndex> then drives the swap.
	UFPSRCardSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSRCardSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}
	FFPSRCardDraw Draw = Sub->BuildSingleDraw(PoolCards[Cap], this);
	if (!Draw.Card)
	{
		return;
	}
	Draw.TargetWeapon = Weapon;
	CachedOffer.Reset();
	CachedOffer.Add(Draw);
	CurrentOfferType = EFPSROfferType::WeaponUnlock;
	++CurrentOfferId;
	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		PS->AddWeaponUnlockPick(); // so ApplyCard's WeaponUnlock pick gate passes when the swap is applied
	}
	ClientPresentCards(CurrentOfferId, CachedOffer);
	NotifyPauseStateDirty();
	UE_LOG(LogFPSR, Warning, TEXT("[Frag] Fill: presented incoming fragment '%s' (offer[0]). Run FPSR.Frag.Replace <dropIndex> (0..%d valid; e.g. 99 to test the server rejecting an out-of-range index)."),
		*PoolFrags[Cap]->GetName(), Cap - 1);
}

void AFPSRPlayerController::DebugSelectFragmentReplacement(int32 DropIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	if (CachedOffer.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Frag] Replace: no active offer — run FPSR.GrantWeaponUnlock first"));
		return;
	}

	// Find the first offered card that grants a behavior fragment (the replacement candidate).
	int32 OfferIndex = INDEX_NONE;
	for (int32 i = 0; i < CachedOffer.Num(); ++i)
	{
		const UFPSRCardDataAsset* Card = CachedOffer[i].Card;
		if (!Card)
		{
			continue;
		}
		for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
		{
			if (Cast<UCardEffect_WeaponBehavior>(Effect)) { OfferIndex = i; break; }
		}
		if (OfferIndex != INDEX_NONE)
		{
			break;
		}
	}
	if (OfferIndex == INDEX_NONE)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Frag] Replace: current offer has no fragment card — re-run FPSR.GrantWeaponUnlock (offers are shuffled)"));
		return;
	}

	UE_LOG(LogFPSR, Warning, TEXT("[Frag] Replace: offer[%d]='%s', dropIndex=%d (server validates the drop index)"),
		OfferIndex, *CachedOffer[OfferIndex].Card->GetName(), DropIndex);
	const bool bApplied = HandleCardSelection(OfferIndex, CurrentOfferId, DropIndex);
	UE_LOG(LogFPSR, Warning, TEXT("[Frag] Replace: %s"),
		bApplied ? TEXT("APPLIED — fragment swapped in") : TEXT("REJECTED — at cap with no valid drop index (anti-cheat / out-of-range)"));
}

namespace
{
	AFPSRPlayerController* GetLocalFPSRController(UWorld* World)
	{
		return World ? Cast<AFPSRPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GCmd_OpeningSeed(
		TEXT("FPSR.OpeningSeed"),
		TEXT("Start the opening-seed card flow (debug, authority/host only). Usage: FPSR.OpeningSeed [count=2]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (!PC || !PC->HasAuthority())
			{
				return;
			}
			int32 Count = 2;
			if (Args.Num() > 0)
			{
				Count = FMath::Max(1, FCString::Atoi(*Args[0]));
			}
			PC->BeginOpeningSeed(Count);
		}));

	FAutoConsoleCommandWithWorld GCmd_GrantWeaponUnlock(
		TEXT("FPSR.GrantWeaponUnlock"),
		TEXT("Grant the local player a weapon-unlock pick (debug, authority/host only) — freezes the run and presents weapon-unlock options."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (!PC || !PC->HasAuthority())
			{
				return;
			}
			PC->GrantWeaponUnlock();
			if (AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr)
			{
				GS->RefreshPauseState();
			}
		}));

	FAutoConsoleCommandWithWorld GCmd_FragFill(
		TEXT("FPSR.Frag.Fill"),
		TEXT("Fill the local player's current weapon to its fragment slot cap (debug, authority/host). Then run FPSR.GrantWeaponUnlock + FPSR.Frag.Replace <dropIndex>."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (PC && PC->HasAuthority())
			{
				PC->DebugFillFragmentSlots();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_FragReplace(
		TEXT("FPSR.Frag.Replace"),
		TEXT("Pick the fragment card in the current offer, dropping the equipped fragment at <dropIndex> (debug). An out-of-range index is server-rejected. Usage: FPSR.Frag.Replace [dropIndex=0]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (!PC || !PC->HasAuthority())
			{
				return;
			}
			const int32 DropIndex = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			PC->DebugSelectFragmentReplacement(DropIndex);
		}));
}

#endif // !UE_BUILD_SHIPPING
