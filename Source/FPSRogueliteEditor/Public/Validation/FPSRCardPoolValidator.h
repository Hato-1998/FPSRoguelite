// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "FPSRCardPoolValidator.generated.h"

/**
 * Cross-asset validation for UFPSRCardPoolDataAsset (P0 data-validation seam). The pool's own IsDataValid
 * (FPSRCardPoolDataAsset.cpp) already catches in-pool CardId duplicates; this validator adds checks that need
 * knowledge OUTSIDE the single asset — every other card pool in the project, and pool-level offer viability.
 * Gated by usecase: on-save runs only cheap self-checks (no cross-project scan on every Ctrl+S), the full cross-pool
 * scan runs on Manual / Commandlet / Script / PreSubmit (see ValidateLoadedAsset_Implementation).
 */
UCLASS()
class UFPSRCardPoolValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;

private:
	/** Cheap, single-asset checks safe to run on every save: null entries, all-zero rarity weights. */
	static EDataValidationResult ValidateCheapSelfChecks(const class UFPSRCardPoolDataAsset* Pool, FDataValidationContext& Context);

	/** Full cross-asset checks: global CardId uniqueness across every card pool in the project, and pool-level
	 *  offer viability (rarity coverage + candidate count after CardFamily de-dup). */
	static EDataValidationResult ValidateCrossPoolChecks(const class UFPSRCardPoolDataAsset* Pool, FDataValidationContext& Context);
};
