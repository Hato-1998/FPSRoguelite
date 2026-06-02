// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRCardDataAsset"

EDataValidationResult UFPSRCardDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (RarityTiers.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoRarityTiers", "Card has no RarityTiers — it will never be offered. Add at least one tier (Rarity + Magnitude)."));
		Result = EDataValidationResult::Invalid;
	}

	if (!AppliedEffect)
	{
		Context.AddWarning(LOCTEXT("NoAppliedEffect", "Card has no AppliedEffect — selecting it will apply nothing."));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
