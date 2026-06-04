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
#include "Card/FPSRCardSubsystem.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRCardSelectWidget.h"

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
	bOpeningSeedIssued = true;
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

	if (XPBarWidgetClass)
	{
		PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Game")), XPBarWidgetClass);
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
	if (PendingOpeningSeeds > 0)
	{
		bOpeningSeedComplete = false;
		RequestCardOffer(false);
	}
	else
	{
		// Nothing to pick — immediately complete so the director's pre-combat hold doesn't wait on this player.
		bOpeningSeedComplete = true;
	}
}

void AFPSRPlayerController::RequestCardOffer(bool bConsumeLevelUp)
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

	CachedOffer = Sub->DrawCards(this);
	bOfferConsumesLevelUp = bConsumeLevelUp;

	if (CachedOffer.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Card] DrawCards returned empty offer (check CardPool)"));
		// An opening-seed offer that can't be drawn (empty pool) must not stall the director's pre-combat
		// hold forever — mark this player's opening seed complete so combat can begin.
		if (!bConsumeLevelUp)
		{
			bOpeningSeedComplete = true;
		}
		ClientDismissCardUI();
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
	const bool bApplied = Sub && Sub->ApplyCard(this, CachedOffer[Index], bOfferConsumesLevelUp);
	if (!bApplied)
	{
		// Application rejected (e.g. no pending pick) — leave the offer up; do not advance or redraw.
		return;
	}

	CachedOffer.Reset();

	// Re-issue the next offer if more picks remain, otherwise dismiss the modal.
	if (!bOfferConsumesLevelUp)
	{
		// Opening seed: present the next seed pick until the count is exhausted.
		if (PendingOpeningSeeds > 0)
		{
			--PendingOpeningSeeds;
		}
		if (PendingOpeningSeeds > 0)
		{
			RequestCardOffer(false);
			return;
		}
		// Opening seed fully consumed — release the director's pre-combat hold for this player.
		bOpeningSeedComplete = true;
	}
	else
	{
		// Breather level-up: keep presenting while this player has pending picks.
		if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
		{
			if (PS->GetCardPicksPending() > 0)
			{
				RequestCardOffer(true);
				return;
			}
		}
	}

	ClientDismissCardUI();
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
		// A rerolled opening-seed offer that comes back empty (exhausted/misconfigured pool) must release the
		// director's pre-combat hold for this player, same as the initial-draw empty path in RequestCardOffer.
		if (!bOfferConsumesLevelUp)
		{
			bOpeningSeedComplete = true;
		}
		ClientDismissCardUI();
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

void AFPSRPlayerController::ServerAbandonOffer_Implementation(int32 OfferId)
{
	// Only the offer the server actually sent can be abandoned (stale/forged ids ignored), so a client
	// can't discard an unfavorable offer for free outside the present-failure path.
	if (OfferId != CurrentOfferId)
	{
		return;
	}

	// If this was an opening-seed offer being abandoned (e.g. UI couldn't present it), mark the opening seed
	// complete so the director's pre-combat hold doesn't wait forever on a player who can't be offered cards.
	if (!bOfferConsumesLevelUp)
	{
		bOpeningSeedComplete = true;
	}

	// Release the cached offer (and any opening-seed run) so PresentPendingLevelUpOffers can retry later.
	CachedOffer.Reset();
	PendingOpeningSeeds = 0;
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

	FAutoConsoleCommandWithWorldAndArgs GCmd_RequestCards(
		TEXT("FPSR.RequestCards"),
		TEXT("Request a breather level-up card offer (debug, authority/host only; consumes a pending pick on selection)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AFPSRPlayerController* PC = GetLocalFPSRController(World);
			if (PC && PC->HasAuthority())
			{
				PC->RequestCardOffer(true);
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
