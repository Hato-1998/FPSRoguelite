// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "Enemy/FPSREnemyBase.h"

#if WITH_AUTOMATION_TESTS

// C1 (ADR 0013 「Docs/Architecture/0013-enemy-tier-axis-and-elite-gas.md」) reparented the two enemy BPs onto the
// new tier-class hierarchy via CoreRedirects. Whether that redirect actually held can ONLY be proven by loading
// the BPs — the automated gate up to here (build + headless unit tests) never does that, so a silently-broken
// redirect would sit undetected until someone opens the editor / hits PIE. This test closes that gap.
//
// Scans Content/Character/Enemy by FOLDER CONVENTION (IFileManager::FindFilesRecursive + FPaths) rather than a
// hardcoded asset-name list — this project's own rule is "no asset paths hardcoded in C++" (CLAUDE.md); a
// hardcoded name list here would silently stop covering a new enemy BP the moment one is added under this folder.
// "Character/Enemy" itself is the folder CONVENTION this test is scoped to (not an individual asset name), so
// referencing that literal is the point, not a violation of the rule. No AssetRegistry module dependency either
// (Build.cs stays untouched): a raw recursive file listing is everything a folder-convention scan needs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyBlueprintParentTest, "FPSRoguelite.Enemy.BlueprintParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyBlueprintParentTest::RunTest(const FString& Parameters)
{
	// The one folder-convention literal this test hardcodes (by design — see the class comment above).
	static const TCHAR* const EnemyFolderConvention = TEXT("Character/Enemy");

	const FString EnemyDir = FPaths::ProjectContentDir() / EnemyFolderConvention;

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *EnemyDir, TEXT("*.uasset"), /*Files=*/true, /*Directories=*/false);

	// Vacuous-pass guard (required, not optional): with zero files found, every "for each found asset" check
	// below trivially runs zero times and the test returns true — indistinguishable from "everything checked out"
	// unless a wrong/empty path fails loudly right here. This session already hit exactly this failure mode once
	// (a broken verification tool read as "no problems found" instead of "checked nothing").
	if (FoundFiles.Num() == 0)
	{
		AddError(FString::Printf(TEXT("Found 0 .uasset files under %s — the scan itself is broken (wrong path or empty folder), which would otherwise make every check below pass vacuously"), *EnemyDir));
		return false;
	}

	for (const FString& FilePath : FoundFiles)
	{
		// Locate the folder-convention segment within the returned disk path and take everything from there on,
		// so deriving the object path never depends on exactly what absolute/relative form FindFilesRecursive's
		// paths happen to take (only on the folder convention itself, which this test is already scoped to).
		FString NormalizedPath = FilePath;
		NormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		const int32 FolderIdx = NormalizedPath.Find(EnemyFolderConvention, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (FolderIdx == INDEX_NONE)
		{
			AddError(FString::Printf(TEXT("Found file %s does not contain the expected folder %s"), *FilePath, EnemyFolderConvention));
			continue;
		}

		// X.uasset -> object path /Game/Character/Enemy/[optional subfolder/]X.X_C (the BP's generated class).
		const FString NoExt = FPaths::GetBaseFilename(NormalizedPath.Mid(FolderIdx), /*bRemovePath=*/false);
		const FString AssetName = FPaths::GetCleanFilename(NoExt);
		const FString ObjectPath = FString::Printf(TEXT("/Game/%s.%s_C"), *NoExt, *AssetName);

		UClass* LoadedClass = LoadObject<UClass>(nullptr, *ObjectPath);

		// (a) Loads at all — a CoreRedirect that failed to fire (stale/typo'd entry, removed target class, ...)
		// surfaces here as a load failure, not as a silently-wrong class.
		if (!TestNotNull(FString::Printf(TEXT("%s loads (%s) — CoreRedirect intact"), *AssetName, *ObjectPath), LoadedClass))
		{
			continue; // nothing further to check against a null class
		}

		// (b) Proves the reparent actually landed ON THE NEW TIER AXIS, not just "loads as something".
		TestTrue(FString::Printf(TEXT("%s reparented onto AFPSREnemyBase"), *AssetName),
			LoadedClass->IsChildOf(AFPSREnemyBase::StaticClass()));

		// (c) P1 regression guard: promoting the 900 StopDistance default into AFPSREnemyBase (ADR 0013 C1) must
		// not have silently reverted a reparented BP to some other stale value.
		if (const AFPSREnemyBase* Cdo = Cast<AFPSREnemyBase>(LoadedClass->GetDefaultObject()))
		{
			TestTrue(FString::Printf(TEXT("%s CDO GetStopDistance() > 0"), *AssetName), Cdo->GetStopDistance() > 0.0f);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s loaded but its CDO is not an AFPSREnemyBase"), *AssetName));
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
