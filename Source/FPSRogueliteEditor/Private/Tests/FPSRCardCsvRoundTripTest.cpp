// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "CardImport/FPSRCardCsvImporter.h"
#include "Card/FPSRCardDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// "CardCsvRoundTrip" suffix keeps this anonymous-namespace helper unique across the unity build (project's
	// test-unity-anon-namespace-collision memory).
	int32 CountDirtyCardPackagesCardCsvRoundTrip(const TArray<FAssetData>& CardAssets)
	{
		int32 DirtyCount = 0;
		for (const FAssetData& AssetData : CardAssets)
		{
			if (UObject* Asset = AssetData.GetAsset())
			{
				if (UPackage* Package = Asset->GetPackage())
				{
					if (Package->IsDirty())
					{
						++DirtyCount;
					}
				}
			}
		}
		return DirtyCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvRoundTripTest, "FPSRoguelite.Editor.CardCsv.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvRoundTripTest::RunTest(const FString& Parameters)
{
	// §12-4: the CSV pipeline's no-regression proof. Content/Authoring/{Cards,CardCatalog}.csv (already exported
	// from the project's existing DA_Card_* assets — B-3) must import back as a total no-op against those SAME
	// assets, twice in a row. bSaveAssets=false throughout: this test must never write to disk or leave a side
	// effect on the working tree — the observable is FFPSRCardImportResult, not a save.
	//
	// ImportAll() always targets the real, fixed Content/Authoring paths (its declared signature takes no path
	// override, §5) — so this test exercises the actually-committed CSV pair, not a scratch fixture. The "export"
	// half of the round trip already happened once, at B-3 commit time (FPSRCardCsvExport::ExportAll was run and
	// its output committed); re-running the exporter here would be redundant with what the CSV on disk already is.

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);
	FARFilter CardFilter;
	CardFilter.ClassPaths.Add(UFPSRCardDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> CardAssets;
	AssetRegistry.GetAssets(CardFilter, CardAssets);
	const int32 ExpectedCardCount = CardAssets.Num();

	TestTrue(TEXT("project has at least one UFPSRCardDataAsset to round-trip"), ExpectedCardCount > 0);
	if (ExpectedCardCount == 0)
	{
		return false;
	}

	// --- Pass 1: import the already-exported CSV against the already-existing assets — must be a total no-op. ---
	const int32 DirtyBeforePass1 = CountDirtyCardPackagesCardCsvRoundTrip(CardAssets);
	const FFPSRCardImportResult First = FPSRCardCsvImport::ImportAll(/*bSaveAssets=*/false);
	for (const FString& Error : First.Errors)
	{
		AddError(FString::Printf(TEXT("pass 1 error: %s"), *Error));
	}
	TestEqual(TEXT("pass 1: no parse/resolution/validation errors"), First.Errors.Num(), 0);
	TestEqual(TEXT("pass 1: zero newly-created cards (CSV already matches every existing asset)"), First.CreatedCount, 0);
	TestEqual(TEXT("pass 1: zero updated cards (import is a no-op against already-exported data)"), First.UpdatedCount, 0);
	TestEqual(TEXT("pass 1: every card reports Unchanged"), First.UnchangedCount, ExpectedCardCount);
	const int32 DirtyAfterPass1 = CountDirtyCardPackagesCardCsvRoundTrip(CardAssets);
	TestEqual(TEXT("pass 1: no NEW dirty card packages (delta vs. before the import call)"), DirtyAfterPass1, DirtyBeforePass1);

	// --- Pass 2: import again — still a no-op (true idempotency, not a first-run coincidence, §12-4). ---
	const int32 DirtyBeforePass2 = DirtyAfterPass1;
	const FFPSRCardImportResult Second = FPSRCardCsvImport::ImportAll(/*bSaveAssets=*/false);
	for (const FString& Error : Second.Errors)
	{
		AddError(FString::Printf(TEXT("pass 2 error: %s"), *Error));
	}
	TestEqual(TEXT("pass 2: no errors"), Second.Errors.Num(), 0);
	TestEqual(TEXT("pass 2: zero created"), Second.CreatedCount, 0);
	TestEqual(TEXT("pass 2: zero updated"), Second.UpdatedCount, 0);
	TestEqual(TEXT("pass 2: every card reports Unchanged"), Second.UnchangedCount, ExpectedCardCount);
	const int32 DirtyAfterPass2 = CountDirtyCardPackagesCardCsvRoundTrip(CardAssets);
	TestEqual(TEXT("pass 2: no NEW dirty card packages"), DirtyAfterPass2, DirtyBeforePass2);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
