// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRMissionTypes.generated.h"

/** Lifecycle state of a mission (replicated to clients for objective UI). */
UENUM(BlueprintType)
enum class EFPSRMissionState : uint8
{
	Inactive,
	Active,
	Completed,
	Failed
};
