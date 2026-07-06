// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "FPSRValidateAnchoredDataCommandlet.generated.h"

/**
 * Headless CI entry point for the anchored data-validation seam (P0). Invoked as:
 *   UnrealEditor-Cmd.exe <uproject> -run=FPSRValidateAnchoredData -unattended -nopause -nullrhi -nosplash -nosound
 * (see Scripts/validate-data.ps1). Validates only the anchors (card pool / run schedule / loadout pool) and every
 * asset reachable from them (FFPSRAnchoredValidationService) — NOT every asset under Content/, so an abandoned
 * draft asset never blocks a build. Orphaned content is reported as a warning, never a failure.
 * Exit code: 0 = all reachable assets valid; 1 = validation failures OR zero anchors found (false-green guard).
 */
UCLASS()
class UFPSRValidateAnchoredDataCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
