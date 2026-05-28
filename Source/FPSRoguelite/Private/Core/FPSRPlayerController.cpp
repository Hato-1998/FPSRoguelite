// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/FPSRLogChannels.h"

AFPSRPlayerController::AFPSRPlayerController()
{
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCFinder(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (IMCFinder.Succeeded())
	{
		DefaultMappingContext = IMCFinder.Object;
	}
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
		UE_LOG(LogFPSR, Error, TEXT("[Input] DefaultMappingContext is NULL (IMC_Default asset failed to load)"));
		return;
	}

	Subsystem->AddMappingContext(DefaultMappingContext, 0);
	UE_LOG(LogFPSR, Warning, TEXT("[Input] Added DefaultMappingContext to local player subsystem"));
}
