// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRMainMenuWidget.h"
#include "Core/FPSRGameFlowSubsystem.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "Kismet/KismetSystemLibrary.h"

UFPSRMainMenuWidget::UFPSRMainMenuWidget()
{
	bIsBackHandler = false;
}

void UFPSRMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// UCommonButtonBase::OnClicked() is a native FCommonButtonEvent (DECLARE_EVENT), not a dynamic
	// multicast — bind with AddUObject (no-arg member), not AddDynamic.
	if (PlayButton)
	{
		PlayButton->OnClicked().AddUObject(this, &UFPSRMainMenuWidget::HandlePlayClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked().AddUObject(this, &UFPSRMainMenuWidget::HandleQuitClicked);
	}
}

TOptional<FUIInputConfig> UFPSRMainMenuWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
}

void UFPSRMainMenuWidget::HandlePlayClicked()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRGameFlowSubsystem* Flow = GI->GetSubsystem<UFPSRGameFlowSubsystem>())
		{
			Flow->StartRun();
		}
	}
}

void UFPSRMainMenuWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
