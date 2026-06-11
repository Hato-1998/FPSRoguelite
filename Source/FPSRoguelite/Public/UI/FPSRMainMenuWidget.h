// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "FPSRMainMenuWidget.generated.h"

class UCommonButtonBase;

/** Main menu widget: Play and Quit buttons.
 *  Pushed to the Menu layer by AFPSRMenuPlayerController.
 *  Handles input mode setup (menu, no cursor capture) via GetDesiredInputConfig. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRMainMenuWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UFPSRMainMenuWidget();

protected:
	virtual void NativeOnInitialized() override;

	/** Establish menu input mode (menu input, no mouse capture). */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonButtonBase> PlayButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonButtonBase> QuitButton;

private:
	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleQuitClicked();
};
