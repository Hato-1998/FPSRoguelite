// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRGameFlowSettings.generated.h"

class UWorld;
class UFPSRLoadoutPoolDataAsset;

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

	/** Lobby level (soft reference). The multiplayer hub: Play always hosts into here (even solo), and every run
	 *  returns here. The host starts a run by ServerTravel-ing from the lobby to RunMap (P7 §3-3). */
	UPROPERTY(EditAnywhere, Config, Category = "Maps", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> LobbyMap;

	/** Selectable weapon pool for the lobby loadout pick (soft reference). Read identically on client (UI list)
	 *  and server (index validation) since DeveloperSettings are config-identical on both — no replication needed. */
	UPROPERTY(EditAnywhere, Config, Category = "Loadout")
	TSoftObjectPtr<UFPSRLoadoutPoolDataAsset> LoadoutPool;

	/** Options string passed to OpenLevel when traveling to the run (e.g., "?listen?beacon=MyBeacon"). */
	UPROPERTY(EditAnywhere, Config, Category = "Travel")
	FString RunTravelOptions;

	/** Helper: resolve a soft map reference to its long package name.
	 *  Returns the package name (e.g., "/Game/Maps/MainMenu") or NAME_None if the input is null.
	 *  Logs an error if the reference is invalid. */
	FName GetLevelPackageName(const TSoftObjectPtr<UWorld>& Map) const;
};
