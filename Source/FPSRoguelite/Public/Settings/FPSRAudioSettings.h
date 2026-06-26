// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSRAudioSettings.generated.h"

class USoundMix;
class USoundClass;

/** Project audio routing config (Project Settings -> "FPSR Audio"). Holds the soft references the audio
 *  subsystem uses to apply the master volume — so NO asset path is hard-coded in C++ (Game.md §6-2). The
 *  values are authored in DefaultGame.ini [/Script/FPSRoguelite.FPSRAudioSettings].
 *
 *  Extensibility-first: adding a category (SFX/Music/UI) = a new child SoundClass + a new soft field here,
 *  no central logic change. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Audio"))
class FPSROGUELITE_API UFPSRAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** The SoundMix that carries the master-volume override (pushed by the audio subsystem). */
	UPROPERTY(Config, EditAnywhere, Category = "Master", meta = (AllowedClasses = "/Script/Engine.SoundMix"))
	TSoftObjectPtr<USoundMix> MasterSoundMix;

	/** The root SoundClass the master override targets (e.g. SC_Master; other classes parent under it). */
	UPROPERTY(Config, EditAnywhere, Category = "Master", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
