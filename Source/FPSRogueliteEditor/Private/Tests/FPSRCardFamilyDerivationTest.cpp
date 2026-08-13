// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "CardImport/FPSRCardCsvExporter.h"
#include "CardImport/FPSRCardCsvSchema.h"
#include "Card/FPSRCardDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#if WITH_AUTOMATION_TESTS

// CARDDRAW v4 (Docs/Specs/CARDDRAW_FamilyRarityExclusion.md §5): the auto-derived CardFamily must be scope-qualified
// for WeaponStat E1 ("<AttrId>.all" / "<AttrId>.this", keyed off the RESOLVED bThisWeaponOnly) and plain "<AttrId>"
// for every other E1 EffectType. Both tests below exercise the REAL derivation code paths (importer + exporter)
// against the project's actual, already-migrated DA_Card_* content rather than synthetic fixtures — the importer's
// DeriveCardFamilyFromE1/ApplyCardFamily and the exporter's blank-normalization comparison are anonymous-namespace
// statics in their .cpp files with no header seam to unit-test directly (project convention, mirrors RoundTripTest).

namespace
{
	// "CardFamilyDerivation" suffix keeps these helpers unique across the unity build (project's
	// test-unity-anon-namespace-collision memory).
	UFPSRCardDataAsset* FindCardByAssetNameCardFamilyDerivation(const TArray<FAssetData>& CardAssets, const TCHAR* AssetName)
	{
		for (const FAssetData& AssetData : CardAssets)
		{
			if (AssetData.AssetName == FName(AssetName))
			{
				return Cast<UFPSRCardDataAsset>(AssetData.GetAsset());
			}
		}
		return nullptr;
	}
}

// --- Test 1: importer-side scope qualification, verified against the migrated real content. ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardFamilyDerivationWeaponScopeTest, "FPSRoguelite.Editor.CardCsv.FamilyDerivation.WeaponStatScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardFamilyDerivationWeaponScopeTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	FARFilter CardFilter;
	CardFilter.ClassPaths.Add(UFPSRCardDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> CardAssets;
	AssetRegistry.GetAssets(CardFilter, CardAssets);

	// Three WeaponStat AllWeapons/ThisWeapon pairs share their raw AttrId (weapon.firerate / weapon.magsize /
	// weapon.recoilvertical) — pre-v4 these collided into ONE family (mutually exclusive in a draw, the bug v4
	// fixes). Post-migration each pair must carry DIFFERENT (scope-qualified) families so they can co-present.
	struct FPair { const TCHAR* AllAsset; const TCHAR* ThisAsset; const TCHAR* AttrId; };
	const FPair Pairs[] = {
		{ TEXT("DA_Card_FireRate_AllWeapon"), TEXT("DA_Card_FireRate_ThisWeapon"), TEXT("weapon.firerate") },
		{ TEXT("DA_Card_MagSize_AllWeapon"), TEXT("DA_Card_MagSize_ThisWeapon"), TEXT("weapon.magsize") },
		{ TEXT("DA_Card_RecoilVertical_AllWeapon"), TEXT("DA_Card_RecoilVertical_ThisWeapon"), TEXT("weapon.recoilvertical") },
	};

	for (const FPair& Pair : Pairs)
	{
		UFPSRCardDataAsset* AllCard = FindCardByAssetNameCardFamilyDerivation(CardAssets, Pair.AllAsset);
		UFPSRCardDataAsset* ThisCard = FindCardByAssetNameCardFamilyDerivation(CardAssets, Pair.ThisAsset);
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Pair.AllAsset), AllCard)
			|| !TestNotNull(*FString::Printf(TEXT("%s exists"), Pair.ThisAsset), ThisCard))
		{
			continue;
		}

		const FString ExpectedAllFamily = FString(Pair.AttrId) + TEXT(".all");
		const FString ExpectedThisFamily = FString(Pair.AttrId) + TEXT(".this");
		TestEqual(*FString::Printf(TEXT("%s CardFamily == '%s'"), Pair.AllAsset, *ExpectedAllFamily), AllCard->CardFamily, FName(*ExpectedAllFamily));
		TestEqual(*FString::Printf(TEXT("%s CardFamily == '%s'"), Pair.ThisAsset, *ExpectedThisFamily), ThisCard->CardFamily, FName(*ExpectedThisFamily));
		TestNotEqual(*FString::Printf(TEXT("%s and %s no longer share a family (v4 fix)"), Pair.AllAsset, Pair.ThisAsset), AllCard->CardFamily, ThisCard->CardFamily);
	}

	// Non-WeaponStat E1 (char.maxhealth, a CharGE card) must NOT get a scope suffix — plain AttrId only.
	if (UFPSRCardDataAsset* MaxHealthCard = FindCardByAssetNameCardFamilyDerivation(CardAssets, TEXT("DA_Card_MaxHealth")))
	{
		TestEqual(TEXT("DA_Card_MaxHealth (CharGE E1) CardFamily == plain 'char.maxhealth', no scope suffix"),
			MaxHealthCard->CardFamily, FName(TEXT("char.maxhealth")));
	}
	else
	{
		AddError(TEXT("DA_Card_MaxHealth not found — cannot verify non-WeaponStat derivation stays unsuffixed"));
	}

	return true;
}

// --- Test 2: exporter-side blank-normalization, exercised against a SCRATCH copy of the real export (never
// touches the committed Content/Authoring/Cards.csv). Confirms the exporter's "is this family the derived one?"
// comparison was updated to the same scope-qualified rule as the importer — otherwise every WeaponStat card would
// churn Cards.csv with a spurious explicit Family on every export (round-trip idempotency break). ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardFamilyDerivationExportBlankTest, "FPSRoguelite.Editor.CardCsv.FamilyDerivation.ExportBlankNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardFamilyDerivationExportBlankTest::RunTest(const FString& Parameters)
{
	const FString ScratchDir = FPaths::ProjectSavedDir() / TEXT("Automation/CardFamilyDerivationScratch/");
	const FString ScratchCardsPath = ScratchDir / TEXT("Cards.csv");
	const FString ScratchCatalogPath = ScratchDir / TEXT("CardCatalog.csv");

	TArray<FString> ExportErrors;
	const bool bExported = FPSRCardCsvExport::ExportAll(ScratchCardsPath, ScratchCatalogPath, ExportErrors);
	for (const FString& Error : ExportErrors)
	{
		AddError(FString::Printf(TEXT("export error: %s"), *Error));
	}
	TestTrue(TEXT("ExportAll succeeds against the real project's cards (scratch output paths)"), bExported);
	if (!bExported)
	{
		return false;
	}

	FString CardsCsvText, CatalogCsvText;
	TestTrue(TEXT("scratch Cards.csv was written"), FFileHelper::LoadFileToString(CardsCsvText, *ScratchCardsPath));
	TestTrue(TEXT("scratch CardCatalog.csv was written"), FFileHelper::LoadFileToString(CatalogCsvText, *ScratchCatalogPath));

	FFPSRCardCsvParseResult ParseResult;
	TestTrue(TEXT("scratch catalog parses"), FPSRCardCsv::ParseCatalog(CatalogCsvText, ParseResult));
	TestTrue(TEXT("scratch cards parse"), FPSRCardCsv::ParseCards(CardsCsvText, ParseResult.Catalog, ParseResult));
	for (const FString& Error : ParseResult.Errors)
	{
		AddError(FString::Printf(TEXT("scratch parse error: %s"), *Error));
	}

	auto FindRow = [&ParseResult](const TCHAR* AssetName) -> const FFPSRCardCsvRow*
	{
		for (const FFPSRCardCsvRow& Row : ParseResult.Cards)
		{
			if (Row.AssetName == AssetName)
			{
				return &Row;
			}
		}
		return nullptr;
	};

	// WeaponStat pair: derived family is scope-qualified, so an unmodified re-export must still leave Family blank.
	const TCHAR* WeaponStatAssets[] = { TEXT("DA_Card_FireRate_AllWeapon"), TEXT("DA_Card_FireRate_ThisWeapon") };
	for (const TCHAR* AssetName : WeaponStatAssets)
	{
		if (const FFPSRCardCsvRow* Row = FindRow(AssetName))
		{
			TestTrue(*FString::Printf(TEXT("%s: Family stays blank (derived value matches the migrated CardFamily)"), AssetName), Row->Family.IsNone());
			if (Row->Effects.Num() > 0)
			{
				TestEqual(*FString::Printf(TEXT("%s: E1_Attr == 'weapon.firerate' (no scope suffix leaks into the CSV attr id)"), AssetName),
					Row->Effects[0].AttrId, FName(TEXT("weapon.firerate")));
			}
		}
		else
		{
			AddError(FString::Printf(TEXT("%s not found in scratch export"), AssetName));
		}
	}

	// Non-WeaponStat: plain-AttrId derivation, also still blank.
	if (const FFPSRCardCsvRow* Row = FindRow(TEXT("DA_Card_MaxHealth")))
	{
		TestTrue(TEXT("DA_Card_MaxHealth: Family stays blank (non-WeaponStat plain-AttrId derivation)"), Row->Family.IsNone());
	}
	else
	{
		AddError(TEXT("DA_Card_MaxHealth not found in scratch export"));
	}

	// Scratch files are throwaway — clean up so repeated runs don't accumulate Saved/ clutter.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteDirectoryRecursively(*ScratchDir);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
