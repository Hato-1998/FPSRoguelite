// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "FPSRRunScheduleValidator.generated.h"

/**
 * Validates a UFPSRRunScheduleDataAsset (P0 data-validation seam). SHALLOW by design: checks only the schedule
 * asset's own fields (mission windows, boss timing, alive-count anchors, spawn-rate fields) — it does NOT load
 * MissionPool entries' MissionClass BP CDOs and does NOT check level/world spawn points (that needs a loaded level,
 * out of scope for an asset validator). Missions validate their own MissionClass via UFPSRMissionDataAsset::IsDataValid.
 */
UCLASS()
class UFPSRRunScheduleValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
