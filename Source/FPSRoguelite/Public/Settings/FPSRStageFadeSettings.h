// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSRStageFadeSettings.generated.h"

class UMaterialInterface;

/** Stage-transition fade routing config (Project Settings -> "FPSR Stage Fade"). Holds the soft reference the
 *  client fade driver (UFPSRStageFadeSubsystem) resolves — so NO asset path is hard-coded in C++ (Game.md §6-2),
 *  matching UFPSRAudioSettings' MasterSoundMix/MasterSoundClass pattern. The value is authored in DefaultGame.ini
 *  [/Script/FPSRoguelite.FPSRStageFadeSettings].
 *
 *  Deliberately holds ONLY the material reference: FadeColor / depth-match tolerance are already exposed as
 *  parameters on the material itself (M_PP_StageFade) — duplicating them here as a runtime override would be a
 *  second place to keep those two numbers in sync for no consumer that needs one. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Stage Fade"))
class FPSROGUELITE_API UFPSRStageFadeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Full-screen post-process material driving the stage-transition fade (BlendableLocation = Scene Color After
	 *  Tonemapping; masks out CustomDepth-equal pixels so the player/enemy silhouettes stay visible through the
	 *  fade — see FadeAlpha usage in UFPSRStageFadeSubsystem). */
	UPROPERTY(Config, EditAnywhere, Category = "Fade", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> StageFadePostProcessMaterial;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
