// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "FPSRSettingsWidget.generated.h"

class USlider;
class UCommonTextBlock;
class UCommonButtonBase;

/** Shared settings overlay (MVP = master volume). Pushed to the Menu layer from the main menu and to the
 *  GameMenu layer in-game (non-pause overlay — 4-player coop never stops the server). The slider drives
 *  UFPSRAudioSubsystem::SetMasterVolume; the value is persisted on release (UFPSRGameUserSettings).
 *
 *  Content (WBP_Settings) subclasses this and binds the widgets. The Back button + CommonUI Back action both
 *  deactivate (pop) the widget. Extensibility: per-category sliders are added in the WBP + a handler here. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRSettingsWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UFPSRSettingsWidget();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;

	/** Menu input mode + visible cursor so the slider is operable (matches the main menu widget). */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	/** Master volume slider (0..1). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> MasterVolumeSlider;

	/** Optional "NN%" readout next to the slider. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonTextBlock> MasterVolumeValueText;

	/** Crosshair thickness slider (0.5..2.0). Optional so a WBP that predates it still binds. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> CrosshairThicknessSlider;

	/** Optional "N.NNx" readout next to the thickness slider. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonTextBlock> CrosshairThicknessValueText;

	/** Crosshair color preset buttons (each sets a fixed color). All optional so the WBP wires whichever it has. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> ColorPresetWhite;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> ColorPresetGreen;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> ColorPresetCyan;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> ColorPresetRed;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> ColorPresetYellow;

	/** Optional explicit Back/Close button (CommonUI Back also closes). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> BackButton;

private:
	/** Live drag: apply without a disk write, refresh the readout. */
	UFUNCTION()
	void HandleMasterVolumeChanged(float Value);

	/** Drag released: persist the current value. */
	UFUNCTION()
	void HandleMasterVolumeCommitted();

	/** Live drag: apply thickness to the settings singleton (broadcasts to HUD), no disk write, refresh readout. */
	UFUNCTION()
	void HandleCrosshairThicknessChanged(float Value);

	/** Drag released: persist the current crosshair thickness. */
	UFUNCTION()
	void HandleCrosshairThicknessCommitted();

	/** Color preset button handlers — each applies + persists a fixed crosshair color. */
	UFUNCTION() void HandleColorPresetWhite();
	UFUNCTION() void HandleColorPresetGreen();
	UFUNCTION() void HandleColorPresetCyan();
	UFUNCTION() void HandleColorPresetRed();
	UFUNCTION() void HandleColorPresetYellow();

	UFUNCTION()
	void HandleBackClicked();

	/** Push the subsystem's current volume into the slider + text (called on activate). */
	void SyncFromSettings();

	/** Read the world's audio subsystem (local UI helper). */
	class UFPSRAudioSubsystem* GetAudioSubsystem() const;

	/** Format a 0..1 scalar as an "NN%" string into the readout. */
	void UpdateValueText(float Value);

	/** Shared: apply + persist a crosshair color preset. */
	void ApplyColorPreset(const FLinearColor& Color);

	/** Format a crosshair thickness multiplier as an "N.NNx" string into the readout. */
	void UpdateThicknessValueText(float Value);
};
