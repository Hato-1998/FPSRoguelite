// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRMainMenuWidget.h"
#include "Core/FPSRSessionSubsystem.h"
#include "Core/FPSRMenuPlayerController.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRFlowLog.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "CommonActivatableWidget.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "GameplayTagContainer.h"
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

	if (SettingsButton)
	{
		SettingsButton->OnClicked().AddUObject(this, &UFPSRMainMenuWidget::HandleSettingsClicked);
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
	FPSRFlowLog::Event(this, TEXT("MENU"), TEXT("Play clicked -> HostSession(4)"));
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

void UFPSRMainMenuWidget::HandleSettingsClicked()
{
	FPSRFlowLog::Event(this, TEXT("MENU"), TEXT("Settings clicked"));
	if (!SettingsWidgetClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Menu] SettingsWidgetClass is not assigned"));
		return;
	}

	const AFPSRMenuPlayerController* MenuPC = Cast<AFPSRMenuPlayerController>(GetOwningPlayer());
	UFPSRPrimaryGameLayout* Layout = MenuPC ? MenuPC->GetPrimaryLayout() : nullptr;
	if (!Layout)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Menu] No PrimaryLayout to push the settings overlay"));
		return;
	}

	// Push onto the Menu layer (above the main menu); the settings widget's Back action pops back to the menu.
	Layout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Menu")), SettingsWidgetClass);
}

void UFPSRMainMenuWidget::HandleQuitClicked()
{
	FPSRFlowLog::Event(this, TEXT("MENU"), TEXT("Quit clicked"));
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
