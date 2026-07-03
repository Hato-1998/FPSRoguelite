// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRCrosshairStyleDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UFPSRCrosshairStyleDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Material.IsNull())
	{
		Context.AddError(FText::FromString(TEXT("CrosshairStyle: Material is unset - the crosshair will not render.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
