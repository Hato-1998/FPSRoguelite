// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "FPSRWeaponValidator.generated.h"

/**
 * Card-routing validation for UFPSRWeaponDataAsset (H2 hard-error routing, mirrors UFPSRCardPoolValidator's shape).
 * Checks that every card wired into the weapon's WeaponCards (level-up) and UnlockableFeatures (mission-clear
 * feature) arrays is actually eligible for that route — a card whose effects don't permit the array it's sitting in
 * would be a silent no-op (or a semantically wrong offer) at draw time. Cheap (no asset-registry scan, just each
 * card's own Effects), so it runs unconditionally — no usecase gating needed, unlike the pool validator's cross-pool
 * pass. Null card entries are skipped here (the weapon's own IsDataValid handles those).
 */
UCLASS()
class UFPSRWeaponValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
