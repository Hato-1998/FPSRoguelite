// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRSettingsWidget.h"
#include "Audio/FPSRAudioSubsystem.h"
#include "Components/Slider.h"
#include "CommonTextBlock.h"
#include "CommonButtonBase.h"
#include "CommonInputModeTypes.h"
#include "Engine/World.h"

void UFPSRSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetMinValue(0.0f);
		MasterVolumeSlider->SetMaxValue(1.0f);
		// OnValueChanged = live drag (apply, no save). OnMouseCaptureEnd = release (persist). Both dynamic.
		MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UFPSRSettingsWidget::HandleMasterVolumeChanged);
		MasterVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UFPSRSettingsWidget::HandleMasterVolumeCommitted);
	}

	if (BackButton)
	{
		// UCommonButtonBase::OnClicked() is a native event — bind with AddUObject (see FPSRMainMenuWidget).
		BackButton->OnClicked().AddUObject(this, &UFPSRSettingsWidget::HandleBackClicked);
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
