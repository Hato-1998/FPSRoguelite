// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRSettingsWidget.h"
#include "UI/FPSRColorPresetButton.h"
#include "UI/FPSRCrosshairColorPresetDataAsset.h"
#include "Audio/FPSRAudioSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "Settings/FPSRGameUserSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Slider.h"
#include "CommonTextBlock.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"

UFPSRSettingsWidget::UFPSRSettingsWidget()
{
	// Handle CommonUI Back (Esc / gamepad-back) → deactivate. Without this the overlay opened via IA_Menu could
	// only be closed by clicking the optional BackButton (Codex merge gate P2).
	bIsBackHandler = true;
}

void UFPSRSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetMinValue(0.0f);
		MasterVolumeSlider->SetMaxValue(1.0f);
		// OnValueChanged = live drag (apply, no save). Capture-end = release → persist. Bind BOTH the mouse and
		// the controller/keyboard capture-end events so gamepad/keyboard adjustments are saved too (Codex P2).
		MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UFPSRSettingsWidget::HandleMasterVolumeChanged);
		MasterVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UFPSRSettingsWidget::HandleMasterVolumeCommitted);
		MasterVolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UFPSRSettingsWidget::HandleMasterVolumeCommitted);
	}

	if (CrosshairThicknessSlider)
	{
		CrosshairThicknessSlider->SetMinValue(0.5f);
		CrosshairThicknessSlider->SetMaxValue(2.0f);
		CrosshairThicknessSlider->OnValueChanged.AddDynamic(this, &UFPSRSettingsWidget::HandleCrosshairThicknessChanged);
		CrosshairThicknessSlider->OnMouseCaptureEnd.AddDynamic(this, &UFPSRSettingsWidget::HandleCrosshairThicknessCommitted);
		CrosshairThicknessSlider->OnControllerCaptureEnd.AddDynamic(this, &UFPSRSettingsWidget::HandleCrosshairThicknessCommitted);
	}

	if (BackButton)
	{
		// UCommonButtonBase::OnClicked() is a native event — bind with AddUObject (see FPSRMainMenuWidget).
		BackButton->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleBackClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleQuitClicked);
	}

	// Colour swatches are generated from the preset DataAsset (no fixed per-colour buttons/handlers in C++ or UMG).
	BuildColorPresetButtons();
}

void UFPSRSettingsWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	SyncFromSettings();
}

TOptional<FUIInputConfig> UFPSRSettingsWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
}

void UFPSRSettingsWidget::SyncFromSettings()
{
	const UFPSRAudioSubsystem* Audio = GetAudioSubsystem();
	const float Volume = Audio ? Audio->GetMasterVolume() : 1.0f;

	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetValue(Volume);
	}
	UpdateValueText(Volume);

	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		const float Thickness = Settings->GetCrosshairThickness();
		if (CrosshairThicknessSlider)
		{
			CrosshairThicknessSlider->SetValue(Thickness);
		}
		UpdateThicknessValueText(Thickness);
	}
}

void UFPSRSettingsWidget::HandleMasterVolumeChanged(float Value)
{
	if (UFPSRAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetMasterVolume(Value, /*bSave=*/false); // live apply, no per-frame disk write
	}
	UpdateValueText(Value);
}

void UFPSRSettingsWidget::HandleMasterVolumeCommitted()
{
	const float Value = MasterVolumeSlider ? MasterVolumeSlider->GetValue() : 1.0f;
	if (UFPSRAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetMasterVolume(Value, /*bSave=*/true); // persist on release
	}
}

void UFPSRSettingsWidget::HandleBackClicked()
{
	DeactivateWidget(); // pops this widget off its layer stack
}

void UFPSRSettingsWidget::HandleQuitClicked()
{
	// Quit to desktop — same call as the main menu's quit (UFPSRMainMenuWidget::HandleQuitClicked). No confirmation
	// prompt yet: add one here if playtests show accidental clicks. As a client this closes the connection, which is
	// what drives the server's Logout handling; as the listen-server host it ends the session for everyone.
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

UFPSRAudioSubsystem* UFPSRSettingsWidget::GetAudioSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UFPSRAudioSubsystem>() : nullptr;
}

void UFPSRSettingsWidget::UpdateValueText(float Value)
{
	if (MasterVolumeValueText)
	{
		const int32 Percent = FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 100.0f);
		MasterVolumeValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Percent)));
	}
}

void UFPSRSettingsWidget::HandleCrosshairThicknessChanged(float Value)
{
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->SetCrosshairThickness(Value, /*bSave=*/false); // live apply (broadcasts to HUD), no per-frame disk write
	}
	UpdateThicknessValueText(Value);
}

void UFPSRSettingsWidget::HandleCrosshairThicknessCommitted()
{
	const float Value = CrosshairThicknessSlider ? CrosshairThicknessSlider->GetValue() : 1.0f;
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->SetCrosshairThickness(Value, /*bSave=*/true); // persist on release
	}
}

void UFPSRSettingsWidget::ApplyColorPreset(const FLinearColor& Color)
{
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->SetCrosshairColor(Color, /*bSave=*/true);
	}
}

void UFPSRSettingsWidget::BuildColorPresetButtons()
{
	const int32 PresetCount = ColorPresets ? ColorPresets->Presets.Num() : 0;

	// The swatch row needs TWO content-authored halves: a preset asset on this WBP and a container to parent the
	// generated buttons into. Warn loudly for either gap — a silently empty row is indistinguishable from "the
	// crosshair colour setting was removed", so a half-migrated WBP must be obvious in the log during content work.
	if (!ColorPresets)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Settings] %s has no ColorPresets asset assigned — crosshair colour swatches are disabled."),
			*GetClass()->GetName());
	}

	if (!ColorPresetContainer)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Settings] WBP binds no ColorPresetContainer — %d authored crosshair colour preset(s) cannot be shown."),
			PresetCount);
		return;
	}

	// Rebuildable: clear first so this is safe if it ever runs more than once.
	ColorPresetContainer->ClearChildren();

	for (int32 Index = 0; Index < PresetCount; ++Index)
	{
		const FFPSRCrosshairColorPreset& Preset = ColorPresets->Presets[Index];

		UFPSRColorPresetButton* Swatch = WidgetTree
			? WidgetTree->ConstructWidget<UFPSRColorPresetButton>(UFPSRColorPresetButton::StaticClass())
			: nullptr;
		if (!Swatch)
		{
			continue;
		}

		// The button IS the swatch: its background carries the preset colour, the tooltip carries the label.
		Swatch->SetBackgroundColor(Preset.Color);
		Swatch->SetToolTipText(Preset.DisplayName);
		Swatch->InitPresetButton(Index);
		Swatch->OnPresetClicked.AddUObject(this, &UFPSRSettingsWidget::ApplyPresetByIndex);

		ColorPresetContainer->AddChild(Swatch);
	}
}

void UFPSRSettingsWidget::ApplyPresetByIndex(int32 Index)
{
	if (ColorPresets && ColorPresets->Presets.IsValidIndex(Index))
	{
		ApplyColorPreset(ColorPresets->Presets[Index].Color);
	}
}

void UFPSRSettingsWidget::UpdateThicknessValueText(float Value)
{
	if (CrosshairThicknessValueText)
	{
		CrosshairThicknessValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2fx"), Value)));
	}
}
