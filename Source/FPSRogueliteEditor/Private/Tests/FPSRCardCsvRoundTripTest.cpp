// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "CardImport/FPSRCardCsvImporter.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// "CardCsvRoundTrip" suffix keeps this anonymous-namespace helper unique across the unity build (project's
	// test-unity-anon-namespace-collision memory). Generalized (P2-⑩) to count ANY tracked asset's dirty package —
	// cards AND pool/weapon, since SyncPoolMembership can dirty those too (a stray add/remove on an otherwise
	// converged run would only show up here, not in a cards-only count).
	int32 CountDirtyPackagesCardCsvRoundTrip(const TArray<FAssetData>& Assets)
	{
		int32 DirtyCount = 0;
		for (const FAssetData& AssetData : Assets)
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
	// assets, twice in a row. bSaveAssets=false throughout: no DataAsset (.uasset) is ever written to disk by this
	// test. That does NOT extend to ST_Card.csv — RegenerateStCardCsv runs unconditionally (it isn't gated by
	// bSaveAssets) and will overwrite the file if its content differs (P2-⑩ comment correction: the earlier
	// wording overstated this as "never write to disk"). On a genuinely converged run it doesn't — Cards.csv's
	// ko/en/ja columns already match what's on disk — but the code path exists and callers should not assume this
	// test is byte-for-byte side-effect-free in the failure case.
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

	// P2-⑩: dirty-package tracking also covers the card pool and every weapon DA — SyncPoolMembership's
	// declarative sync writes to THOSE packages, not the card packages, so a cards-only dirty count would miss a
	// spurious add/remove on an otherwise-converged run.
	FARFilter PoolFilter;
	PoolFilter.ClassPaths.Add(UFPSRCardPoolDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> PoolAssets;
	AssetRegistry.GetAssets(PoolFilter, PoolAssets);

	FARFilter WeaponFilter;
	WeaponFilter.ClassPaths.Add(UFPSRWeaponDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> WeaponAssets;
	AssetRegistry.GetAssets(WeaponFilter, WeaponAssets);

	TArray<FAssetData> AllTrackedAssets;
	AllTrackedAssets.Reserve(CardAssets.Num() + PoolAssets.Num() + WeaponAssets.Num());
	AllTrackedAssets.Append(CardAssets);
	AllTrackedAssets.Append(PoolAssets);
	AllTrackedAssets.Append(WeaponAssets);

	// --- Pass 1: import the already-exported CSV against the already-existing assets — must be a total no-op. ---
	const int32 DirtyBeforePass1 = CountDirtyPackagesCardCsvRoundTrip(AllTrackedAssets);
	const FFPSRCardImportResult First = FPSRCardCsvImport::ImportAll(/*bSaveAssets=*/false);
	for (const FString& Error : First.Errors)
	{
		AddError(FString::Printf(TEXT("pass 1 error: %s"), *Error));
	}
	TestEqual(TEXT("pass 1: no parse/resolution/validation errors"), First.Errors.Num(), 0);
	TestEqual(TEXT("pass 1: zero newly-created cards (CSV already matches every existing asset)"), First.CreatedCount, 0);
	TestEqual(TEXT("pass 1: zero updated cards (import is a no-op against already-exported data)"), First.UpdatedCount, 0);
	TestEqual(TEXT("pass 1: every card reports Unchanged"), First.UnchangedCount, ExpectedCardCount);
	TestEqual(TEXT("pass 1: zero pool/weapon membership removed (already converged)"), First.RemovedMembershipCount, 0);
	const int32 DirtyAfterPass1 = CountDirtyPackagesCardCsvRoundTrip(AllTrackedAssets);
	TestEqual(TEXT("pass 1: no NEW dirty packages — cards + pool + weapons (delta vs. before the import call)"), DirtyAfterPass1, DirtyBeforePass1);

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
	TestEqual(TEXT("pass 2: zero pool/weapon membership removed"), Second.RemovedMembershipCount, 0);
	const int32 DirtyAfterPass2 = CountDirtyPackagesCardCsvRoundTrip(AllTrackedAssets);
	TestEqual(TEXT("pass 2: no NEW dirty packages — cards + pool + weapons"), DirtyAfterPass2, DirtyBeforePass2);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
