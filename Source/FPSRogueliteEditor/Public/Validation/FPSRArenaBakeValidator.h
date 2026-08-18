// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "FPSRArenaBakeValidator.generated.h"

/**
 * Blocks a level whose arena bake no longer matches its geometry (ADR 0012, check ③ of five).
 *
 * This is the check that pays for choosing a separate bake DataAsset over storing the mask inside the sublevel.
 * The inline option could not go stale — the mask lived in the same file as the geometry — but buying that
 * needed a save-time auto-bake hook. Splitting them is cheaper and merges cleanly, and the price is that a
 * person can now edit a wall and forget to re-bake. This validator is where that price is paid.
 *
 * It runs on Manual / Commandlet / Script / PreSubmit like the other validators in this folder, so a stale bake
 * is stopped BEFORE it reaches anyone else — which is stricter than the save-time warning it backs up, since a
 * warning can be clicked through and a failed submit cannot.
 *
 * Why a level validator rather than a check on the bake asset: the level is what changed. Asking the asset
 * "are you current?" would make it load and scan a level it merely names, and it would pass silently for an
 * arena whose asset reference was never assigned at all — the most common authoring mistake of the three.
 */
UCLASS()
class UFPSRArenaBakeValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset,
		FDataValidationContext& InContext) const override;

	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset,
		FDataValidationContext& Context) override;
};
