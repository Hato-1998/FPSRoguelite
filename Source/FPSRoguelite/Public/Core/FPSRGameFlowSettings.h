// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRGameFlowSettings.generated.h"

class UWorld;

/** Game flow configuration: level packages for menu and run, travel options.
 *  Editable in Project Settings > Game > FPSR Game Flow. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Game Flow"))
class FPSROGUELITE_API UFPSRGameFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	/** Main menu level (soft reference). The game starts here and returns after runs complete. */
	UPROPERTY(EditAnywhere, Config, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> MainMenuMap;

	/** Run level (soft reference). The gameplay loop executes here; director handles progression. */
	UPROPERTY(EditAnywhere, Config, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> RunMap;

	/** Options string passed to OpenLevel when traveling to the run (e.g., "?listen?beacon=MyBeacon"). */
	UPROPERTY(EditAnywhere, Config, Category = "Travel")
	FString RunTravelOptions;

	/** Helper: resolve a soft map reference to its long package name.
	 *  Returns the package name (e.g., "/Game/Maps/MainMenu") or NAME_None if the input is null.
	 *  Logs an error if the reference is invalid. */
	FName GetLevelPackageName(const TSoftObjectPtr<UWorld>& Map) const;
};
