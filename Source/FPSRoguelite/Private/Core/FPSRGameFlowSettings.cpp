// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"

FName UFPSRGameFlowSettings::GetCategoryName() const
{
	return FName("Game");
}

FName UFPSRGameFlowSettings::GetLevelPackageName(const TSoftObjectPtr<UWorld>& Map) const
{
	if (Map.IsNull())
	{
		UE_LOG(LogFPSR, Error, TEXT("[GameFlow] Map reference is null"));
		return NAME_None;
	}

	return Map.ToSoftObjectPath().GetLongPackageFName();
}
