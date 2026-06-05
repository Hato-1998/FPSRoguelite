// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRMissionDataAsset"

EDataValidationResult UFPSRMissionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!MissionClass)
	{
		Context.AddError(LOCTEXT("NoMissionClass", "Mission has no MissionClass — no actor will be spawned. Set a mission actor class."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
