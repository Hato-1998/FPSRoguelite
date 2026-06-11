// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRResultWidget.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRGameFlowSubsystem.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"

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
	CachedOutcome = Outcome;
	OnOutcomeSet(Outcome);
}

void UFPSRResultWidget::HandleReturnClicked()
{
	AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(GetOwningPlayer());
	if (!PC)
	{
		return;
	}

	if (PC->HasAuthority())
	{
		// Authority: call the subsystem directly.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UFPSRGameFlowSubsystem* Flow = GI->GetSubsystem<UFPSRGameFlowSubsystem>())
			{
				Flow->ReturnToMenu(CachedOutcome);
			}
		}
	}
	else
	{
		// Non-authority client: send RPC to server.
		PC->ServerRequestReturnToMenu(CachedOutcome);
	}
}
