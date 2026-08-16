// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaParamsDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRArenaParams"

FFPSRArenaGenParams UFPSRArenaParamsDataAsset::ToGenParams() const
{
	FFPSRArenaGenParams Out;
	Out.ArenaSizeCells = ArenaSizeCells;
	Out.CellSize = CellSize;
	Out.ClimbableStepHeight = ClimbableStepHeight;
	Out.SlotGridOptions = SlotGridOptions;
	Out.MinCorridorWidthCells = MinCorridorWidthCells;
	Out.BoundaryMarginCells = BoundaryMarginCells;
	Out.ClusterFillMin = ClusterFillMin;
	Out.ClusterFillMax = ClusterFillMax;
	Out.BlockingPropsPer100Cells = BlockingPropsPer100Cells;
	Out.PassablePropsPer100Cells = PassablePropsPer100Cells;
	Out.PropMinSpacingCells = PropMinSpacingCells;
	Out.MaxSlackConsumption = MaxSlackConsumption;
	Out.PropVariantCount = PropVariantCount;
	return Out;
}

#if WITH_EDITOR
EDataValidationResult UFPSRArenaParamsDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Reuse the runtime check rather than restating it: a rule that exists in two places is a rule that will
	// disagree with itself. This is the same predicate Generate() fails-fast on, so anything that validates
	// here is guaranteed to generate.
	FString Error;
	if (!ToGenParams().Validate(Error))
	{
		Context.AddError(FText::FromString(Error));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
