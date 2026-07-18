// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRCrosshairColorPresetDataAsset.generated.h"

/** One authored crosshair colour swatch shown in the settings overlay. */
USTRUCT(BlueprintType)
struct FFPSRCrosshairColorPreset
{
	GENERATED_BODY()

	/** Label shown as the swatch's tooltip (localisable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FText DisplayName;

	/** Colour applied to the crosshair when this swatch is clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FLinearColor Color = FLinearColor::White;
};

/** Designer-authored crosshair colour presets (Game.md §6-2: values live in data, not C++).
 *
 *  UFPSRSettingsWidget generates ONE swatch button per entry at init, so adding, removing, re-ordering or
 *  re-colouring a preset is a pure data edit — no C++ recompile and no UMG change. Replaces the former five
 *  hard-coded FLinearColor handlers + five fixed WBP buttons. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCrosshairColorPresetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Swatches in display order. Empty = no colour row is shown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair")
	TArray<FFPSRCrosshairColorPreset> Presets;
};
