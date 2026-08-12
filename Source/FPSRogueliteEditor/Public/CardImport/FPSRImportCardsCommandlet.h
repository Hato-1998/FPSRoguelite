// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "FPSRImportCardsCommandlet.generated.h"

/**
 * Headless entry point for the card CSV importer (§2-3-10, §5), mirroring
 * UFPSRValidateAnchoredDataCommandlet's pattern. Invoked as:
 *   UnrealEditor-Cmd.exe <uproject> -run=FPSRImportCards -unattended -nopause -nullrhi -nosplash -nosound
 * Runs FPSRCardCsvImport::ImportAll(true) (saves on success), logs every collected error, and returns the error
 * count as the process exit code (0 = success).
 */
UCLASS()
class UFPSRImportCardsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
