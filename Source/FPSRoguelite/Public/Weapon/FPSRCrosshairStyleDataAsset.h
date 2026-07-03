// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FPSRCrosshairStyleDataAsset.generated.h"

class UMaterialInterface;

/** A designer-authored crosshair style: the procedural crosshair material plus its spread behaviour.
 *  Weapons reference one of these (FPSRWeaponDataAsset::CrosshairStyle) instead of picking a raw material,
 *  so the style library reads as a clean named dropdown and a new style is pure data (a new asset, no code
 *  change). The HUD drives only the per-frame Spread parameter on a dynamic copy of Material; all shape /
 *  tuning lives in Material (or a child MI). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCrosshairStyleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Designer-facing name (e.g. "Cross", "Shotgun Ring", "Launcher Box", "Dot"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair")
	FText DisplayName;

	/** Procedural crosshair material (SDF) or a child MI of one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> Material;

	/** true = dynamic (fire bloom widens the crosshair via the Spread parameter); false = static (no spread). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair")
	bool bDynamic = true;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
