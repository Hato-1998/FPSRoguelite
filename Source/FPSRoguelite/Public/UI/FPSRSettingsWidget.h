// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "FPSRSettingsWidget.generated.h"

class USlider;
class UCommonTextBlock;
class UCommonButtonBase;
class UPanelWidget;
class UFPSRCrosshairColorPresetDataAsset;

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

	/** Panel the crosshair colour swatches are generated into — one button per entry in ColorPresets, built at
	 *  init. Optional so a WBP that predates the data-driven presets still binds (the row is then just absent). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ColorPresetContainer;

	/** Designer-authored crosshair colour swatches. Adding/removing/re-colouring a preset is a pure data edit —
	 *  no C++ and no UMG change (the buttons are generated from this list). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Crosshair")
	TObjectPtr<UFPSRCrosshairColorPresetDataAsset> ColorPresets;

	/** Optional explicit Back/Close button (CommonUI Back also closes). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> BackButton;

	/** Optional "Quit game" button — exits to desktop. The overlay is reachable during the card-select freeze and
	 *  while downed (see AFPSRCharacter's menu input), so this is also the in-game way for a client to leave a
	 *  listen-server session at any moment: the process closes, the connection drops, and the server runs its
	 *  Logout path (wipe / freeze recompute). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> QuitButton;

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

	/** Generate one swatch button per entry in ColorPresets into ColorPresetContainer (called once at init). */
	void BuildColorPresetButtons();

	/** Apply + persist the crosshair colour of the preset at Index (bound per generated swatch). */
	void ApplyPresetByIndex(int32 Index);

	UFUNCTION()
	void HandleBackClicked();

	/** Quit to desktop (same call the main menu's quit uses). */
	UFUNCTION()
	void HandleQuitClicked();

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
