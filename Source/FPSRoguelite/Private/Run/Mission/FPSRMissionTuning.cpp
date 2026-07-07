// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionTuning.h"

#if WITH_EDITOR

FText UFPSRMissionTuning::GetEditorSummary() const
{
	return GetClass()->GetDisplayNameText();
}

#endif // WITH_EDITOR
