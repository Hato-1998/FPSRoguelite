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
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameState.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRCardSelectWidget.h"
#include "UI/FPSRGameHUDWidget.h"
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

	if (!IsLocalPlayerController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Input] EnhancedInputLocalPlayerSubsystem not found on PlayerController"));
		return;
	}

	if (!DefaultMappingContext)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Input] DefaultMappingContext is NULL (assign it in BP_FPSRPlayerController)"));
		return;
	}

	Subsystem->AddMappingContext(DefaultMappingContext, 0);
	UE_LOG(LogFPSR, Verbose, TEXT("[Input] Added DefaultMappingContext to local player subsystem"));
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

void AFPSRPlayerController::GrantMissionReward(UFPSRCardDataAsset* RewardCard)
{
	if (!HasAuthority())
	{
		return;
	}

	// Mission clear always grants a reward pick. The default reward is a choice from the equipped weapon's
	// AvailableModifiers (resolved at offer time); a non-null RewardCard is an optional per-mission override.
	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		PS->AddMissionRewardPick();
		if (RewardCard)
		{
			PendingMissionRewardCards.Add(RewardCard);
		}
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
		return PS->GetMissionRewardPicksPending() > 0 || PS->GetCardPicksPending() > 0;
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
	if (PS && PS->GetMissionRewardPicksPending() > 0)
	{
		RequestCardOffer(EFPSROfferType::MissionReward);
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

	if (OfferType == EFPSROfferType::MissionReward)
	{
		CachedOffer.Reset();
		if (PendingMissionRewardCards.Num() > 0)
		{
			// Per-mission override: single designer-specified reward card from the front of the queue.
			const FFPSRCardDraw Draw = Sub->BuildSingleDraw(PendingMissionRewardCards[0].Get(), this);
			if (Draw.Card)
			{
				CachedOffer.Add(Draw);
			}
		}
		else
		{
			// Default: a choice from the equipped weapon's AvailableModifiers (unowned behavior fragments).
			CachedOffer = Sub->DrawWeaponModifierOffer(this, 3);
		}
	}
	else
	{
		CachedOffer = Sub->DrawCards(this);
	}

	if (CachedOffer.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Card] Empty offer for type %d (check pool/reward card)"), (int32)OfferType);
		// Can't present this selection — release it so the global freeze doesn't hard-lock on this player.
		if (OfferType == EFPSROfferType::OpeningSeed)
		{
			PendingOpeningSeeds = 0;
		}
		else if (OfferType == EFPSROfferType::MissionReward)
		{
			if (PendingMissionRewardCards.Num() > 0) { PendingMissionRewardCards.RemoveAt(0); }
			if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>()) { PS->ConsumeMissionRewardPick(); }
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
	// Ignore stale/duplicate selections (double-click, spam) that don't match the current offer.
	if (OfferId != CurrentOfferId)
	{
		return;
	}

	// Ignore invalid intent — keep the current offer up so the client can't force a free redraw.
	if (!CachedOffer.IsValidIndex(Index))
	{
		return;
	}

	UFPSRCardSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSRCardSubsystem>() : nullptr;
	const bool bApplied = Sub && Sub->ApplyCard(this, CachedOffer[Index], CurrentOfferType);
	if (!bApplied)
	{
		// Application rejected (e.g. no pending pick) — leave the offer up; do not advance or redraw.
		return;
	}

	// Per-type bookkeeping (the level-up / mission-reward pick was consumed inside ApplyCard).
	if (CurrentOfferType == EFPSROfferType::OpeningSeed)
	{
		if (PendingOpeningSeeds > 0) { --PendingOpeningSeeds; }
	}
	else if (CurrentOfferType == EFPSROfferType::MissionReward)
	{
		if (PendingMissionRewardCards.Num() > 0) { PendingMissionRewardCards.RemoveAt(0); }
	}

	CachedOffer.Reset();

	// Re-present this player's next pick (if any) and recompute the global freeze (unpause when all done).
	NotifyPauseStateDirty();
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

	// Mission-reward offers are a single fixed card — not rerollable.
	if (CurrentOfferType == EFPSROfferType::MissionReward)
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
	PendingMissionRewardCards.Reset();
	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		while (PS->ConsumeCardPick()) {}
		while (PS->ConsumeMissionRewardPick()) {}
	}

	NotifyPauseStateDirty();
}

#if !UE_BUILD_SHIPPING

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

	FAutoConsoleCommandWithWorld GCmd_GrantMissionRewardPick(
		TEXT("FPSR.GrantMissionRewardPick"),
		TEXT("Grant the local player a mission-reward pick (debug, authority/host only) — freezes the run and "
		     "offers a behavior-fragment choice from the equipped weapon's AvailableModifiers."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (!PC || !PC->HasAuthority())
			{
				return;
			}
			PC->GrantMissionReward(nullptr); // null = default weapon-modifier choice (no override card)
			if (AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr)
			{
				GS->RefreshPauseState(); // freeze + present the reward offer
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
