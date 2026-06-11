// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRGameFlowTypes.generated.h"

/** Outcome of a run; used to gate menu/result UI transitions. */
UENUM(BlueprintType)
enum class EFPSRRunOutcome : uint8
{
	None,      // No outcome yet (initial state).
	Victory,   // Run completed successfully.
	Defeat     // Run ended in failure.
};
