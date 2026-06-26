// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "FPSRMainMenuWidget.generated.h"

class UCommonButtonBase;
class UCommonActivatableWidget;

/** Main menu widget: Play, Settings and Quit buttons.
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

	/** Optional so the existing WBP keeps compiling until the Settings button is added (content step). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> SettingsButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonButtonBase> QuitButton;

	/** Shared settings overlay pushed to the Menu layer on Settings click. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UCommonActivatableWidget> SettingsWidgetClass;

private:
	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitClicked();
};
