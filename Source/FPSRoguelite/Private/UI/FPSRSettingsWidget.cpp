// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRSettingsWidget.h"
#include "Audio/FPSRAudioSubsystem.h"
#include "Settings/FPSRGameUserSettings.h"
#include "Components/Slider.h"
#include "CommonTextBlock.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "Engine/World.h"

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

	// Color presets — each is a fixed-color CommonButton. Bind the same way as BackButton (native event).
	if (ColorPresetWhite)
	{
		ColorPresetWhite->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleColorPresetWhite);
	}
	if (ColorPresetGreen)
	{
		ColorPresetGreen->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleColorPresetGreen);
	}
	if (ColorPresetCyan)
	{
		ColorPresetCyan->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleColorPresetCyan);
	}
	if (ColorPresetRed)
	{
		ColorPresetRed->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleColorPresetRed);
	}
	if (ColorPresetYellow)
	{
		ColorPresetYellow->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleColorPresetYellow);
	}
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

void UFPSRSettingsWidget::HandleColorPresetWhite()  { ApplyColorPreset(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)); }
void UFPSRSettingsWidget::HandleColorPresetGreen()  { ApplyColorPreset(FLinearColor(0.10f, 1.0f, 0.10f, 1.0f)); }
void UFPSRSettingsWidget::HandleColorPresetCyan()   { ApplyColorPreset(FLinearColor(0.10f, 1.0f, 1.0f, 1.0f)); }
void UFPSRSettingsWidget::HandleColorPresetRed()    { ApplyColorPreset(FLinearColor(1.0f, 0.10f, 0.10f, 1.0f)); }
void UFPSRSettingsWidget::HandleColorPresetYellow() { ApplyColorPreset(FLinearColor(1.0f, 1.0f, 0.10f, 1.0f)); }

void UFPSRSettingsWidget::UpdateThicknessValueText(float Value)
{
	if (CrosshairThicknessValueText)
	{
		CrosshairThicknessValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2fx"), Value)));
	}
}
