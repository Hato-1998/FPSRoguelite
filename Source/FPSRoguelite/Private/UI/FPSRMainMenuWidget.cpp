// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRMainMenuWidget.h"
#include "Core/FPSRSessionSubsystem.h"
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
	// Play always hosts a session and travels into the lobby hub — even solo (1-player lobby). The lobby is the
	// hub for every run; the host starts the run from there (P7 §3-3, user-confirmed).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRSessionSubsystem* Session = GI->GetSubsystem<UFPSRSessionSubsystem>())
		{
			Session->HostSession(4);
		}
	}
}

void UFPSRMainMenuWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
