// Copyright Epic Games, Inc. All Rights Reserved.

#include "CardImport/FPSRImportCardsCommandlet.h"
#include "CardImport/FPSRCardCsvImporter.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRImportCards, Log, All);

int32 UFPSRImportCardsCommandlet::Main(const FString& Params)
{
	check(GEditor);

	UE_LOG(LogFPSRImportCards, Log, TEXT("--------------------------------------------------------------------------------------------"));
	UE_LOG(LogFPSRImportCards, Log, TEXT("FPSRImportCards: starting"));

	const FFPSRCardImportResult Result = FPSRCardCsvImport::ImportAll(/*bSaveAssets=*/true);

	for (const FString& Error : Result.Errors)
	{
		UE_LOG(LogFPSRImportCards, Error, TEXT("FPSRImportCards: %s"), *Error);
	}

	UE_LOG(LogFPSRImportCards, Log, TEXT("FPSRImportCards: created=%d updated=%d unchanged=%d removedMembership=%d errors=%d"),
		Result.CreatedCount, Result.UpdatedCount, Result.UnchangedCount, Result.RemovedMembershipCount, Result.Errors.Num());
	UE_LOG(LogFPSRImportCards, Log, TEXT("--------------------------------------------------------------------------------------------"));

	return Result.Errors.Num();
}
