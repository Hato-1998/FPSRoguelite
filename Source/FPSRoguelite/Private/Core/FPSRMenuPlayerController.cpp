// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRMenuPlayerController.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRUITags.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "Core/FPSRLogChannels.h"

void AFPSRMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (!PrimaryLayoutClass)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Menu] PrimaryLayoutClass is not assigned"));
		return;
	}

	PrimaryLayout = CreateWidget<UFPSRPrimaryGameLayout>(this, PrimaryLayoutClass);
	if (!PrimaryLayout)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Menu] Failed to create PrimaryGameLayout"));
		return;
	}

	PrimaryLayout->AddToViewport();

	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Menu] MainMenuWidgetClass is not assigned"));
		return;
	}

	UCommonActivatableWidget* MenuWidget =
		PrimaryLayout->PushWidgetToLayer(FPSRUITags::TAG_UI_Layer_Menu.GetTag(), MainMenuWidgetClass);

	if (!MenuWidget)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Menu] Failed to push MainMenuWidget to Menu layer"));
	}
}
