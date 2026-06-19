// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRResultWidget.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRGameMode.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "Engine/World.h"

UFPSRResultWidget::UFPSRResultWidget()
{
	bIsBackHandler = false;
}

void UFPSRResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Native FCommonButtonEvent (DECLARE_EVENT) — bind with AddUObject, not AddDynamic.
	if (ReturnButton)
	{
		ReturnButton->OnClicked().AddUObject(this, &UFPSRResultWidget::HandleReturnClicked);
	}
}

TOptional<FUIInputConfig> UFPSRResultWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
}

void UFPSRResultWidget::SetOutcome(EFPSRRunOutcome Outcome)
{
	OnOutcomeSet(Outcome);
}

void UFPSRResultWidget::HandleReturnClicked()
{
	AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(GetOwningPlayer());
	if (!PC)
	{
		return;
	}

	// Return goes to the LOBBY hub, not the main menu (P7 §3-6 — every run returns to the lobby; this matches the
	// GameMode's automatic post-run travel and just fires it now instead of waiting out PostRunTravelDelay).
	if (PC->HasAuthority())
	{
		if (AFPSRGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRGameMode>() : nullptr)
		{
			GM->RequestReturnToLobby();
		}
	}
	else
	{
		PC->ServerRequestReturnToLobby();
	}
}
