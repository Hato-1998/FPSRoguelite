// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/FPSRGameUserSettings.h"
#include "Engine/Engine.h"

UFPSRGameUserSettings::UFPSRGameUserSettings()
{
	MasterVolume = 1.0f;
	CrosshairColor = FLinearColor::White;
	CrosshairThickness = 1.0f;
}

UFPSRGameUserSettings* UFPSRGameUserSettings::Get()
{
	// GEngine->GetGameUserSettings() returns the instance of the class registered via
	// [/Script/Engine.Engine] GameUserSettingsClassName — i.e. this subclass when the config is set.
	return GEngine ? Cast<UFPSRGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UFPSRGameUserSettings::SetMasterVolume(float InVolume, bool bSave)
{
	MasterVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	if (bSave)
	{
		SaveSettings();
	}
}

void UFPSRGameUserSettings::SetCrosshairColor(FLinearColor InColor, bool bSave)
{
	CrosshairColor = InColor;
	// Broadcast unconditionally so a live preview re-tints the HUD crosshair even before the value is persisted.
	OnCrosshairSettingsChanged.Broadcast();
	if (bSave)
	{
		SaveSettings();
	}
}

void UFPSRGameUserSettings::SetCrosshairThickness(float InThickness, bool bSave)
{
	CrosshairThickness = FMath::Clamp(InThickness, 0.5f, 2.0f);
	// Broadcast unconditionally so a live drag re-applies to the HUD crosshair before persistence.
	OnCrosshairSettingsChanged.Broadcast();
	if (bSave)
	{
		SaveSettings();
	}
}
