// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "FPSRLoadoutPoolValidator.generated.h"

/**
 * Validates a UFPSRLoadoutPoolDataAsset in isolation: the lobby loadout list must be non-empty,
 * have no null entries, and no duplicate weapon entries (the selection RPC indexes into it, so a
 * null/duplicate is a silent content bug). Weapon internals are validated by the weapon DA's own
 * IsDataValid — this validator only owns the loadout-list contract.
 */
UCLASS()
class UFPSRLoadoutPoolValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
