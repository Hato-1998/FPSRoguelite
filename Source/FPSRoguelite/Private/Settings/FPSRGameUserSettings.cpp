// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/FPSRGameUserSettings.h"
#include "Engine/Engine.h"

UFPSRGameUserSettings::UFPSRGameUserSettings()
{
	MasterVolume = 1.0f;
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
