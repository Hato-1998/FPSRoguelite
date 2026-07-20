// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/FPSRBlockoutSettings.h"

UFPSRBlockoutSettings::UFPSRBlockoutSettings()
{
	// Config default only: applies when Config/DefaultEditor.ini has no [/Script/FPSRogueliteEditor.FPSRBlockoutSettings]
	// entry yet. Designers change/extend this in Project Settings > FPSR > FPSR Blockout (no rebuild).
	FDirectoryPath DefaultFolder;
	DefaultFolder.Path = TEXT("/Game/PolygonCyberCity/Meshes");
	PaletteFolders.Add(DefaultFolder);

	PrefabSaveFolder.Path = TEXT("/Game/CityPrefabs");
}

FName UFPSRBlockoutSettings::GetCategoryName() const
{
	return FName(TEXT("FPSR"));
}
