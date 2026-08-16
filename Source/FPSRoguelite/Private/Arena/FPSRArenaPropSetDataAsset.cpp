// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaPropSetDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRArenaPropSet"

FFPSRArenaPropSet UFPSRArenaPropSetDataAsset::ToPropSet() const
{
	FFPSRArenaPropSet Out;
	Out.Entries = Entries;
	Out.Weight = FMath::Max(1, Weight);
	Out.bAllowRotation = bAllowRotation;

	// Derived, never authored: the fit test trusts this box to contain every entry, and a hand-typed one that
	// was a cell too small would let a group overhang into a corridor the test had already called clear.
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Max = FIntPoint::ZeroValue;
	for (const FFPSRArenaPropSetEntry& E : Entries)
	{
		Min.X = FMath::Min(Min.X, E.CellOffset.X);
		Min.Y = FMath::Min(Min.Y, E.CellOffset.Y);
		Max.X = FMath::Max(Max.X, E.CellOffset.X);
		Max.Y = FMath::Max(Max.Y, E.CellOffset.Y);
	}
	Out.FootprintCells = FIntPoint(Max.X - Min.X + 1, Max.Y - Min.Y + 1);
	return Out;
}

#if WITH_EDITOR
EDataValidationResult UFPSRArenaPropSetDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Entries.Num() == 0)
	{
		Context.AddError(LOCTEXT("EmptySet", "프롭 세트가 비어 있습니다 — 구성 프롭을 최소 하나 넣으세요."));
		Result = EDataValidationResult::Invalid;
	}

	// Two entries on the same cell would stack geometry and, if their tiers differ, make "what height is this
	// cell" unanswerable — which is exactly the question the 45/60 band depends on.
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		for (int32 j = i + 1; j < Entries.Num(); ++j)
		{
			if (Entries[i].CellOffset == Entries[j].CellOffset)
			{
				Context.AddError(FText::Format(
					LOCTEXT("DuplicateCell", "구성 프롭 {0}번과 {1}번이 같은 셀({2},{3})에 있습니다."),
					FText::AsNumber(i), FText::AsNumber(j),
					FText::AsNumber(Entries[i].CellOffset.X), FText::AsNumber(Entries[i].CellOffset.Y)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
