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
