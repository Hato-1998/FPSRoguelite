// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "Enemy/FPSREnemyBase.h"
#include "Core/FPSRLogChannels.h" // LogFPSR — the coverage lines below (see the scan-count comment for why)
#include "Components/WidgetComponent.h" // HB1 §12-4: UWidgetComponent count check
#include "Engine/Blueprint.h" // UBlueprint check: tells a non-Blueprint asset (e.g. a DataAsset sharing this folder) apart from an actual CoreRedirect break
#include "Engine/BlueprintGeneratedClass.h" // HB1 §12-4: walk each BP level's own SCS (mirrors SFPSRBlockoutTab.cpp's CDO+SCS pattern)
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

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

	// Report the COVERAGE, not just the verdict. The guard above only proves "more than zero" — if the scan silently
	// returned one asset instead of two, every check below would still pass and the unchecked BP would look covered.
	// A pass is only as trustworthy as the count it examined, so put that count (and each asset's resolved parent) in
	// the log where a reviewer can read it back.
	UE_LOG(LogFPSR, Log, TEXT("[Test] BlueprintParent: scanning %s — %d .uasset file(s) found"), *EnemyDir, FoundFiles.Num());

	// Counts for the second vacuous-pass guard (and coverage log) after the loop: once check (a) below can
	// legitimately skip a found file that turns out not to be a Blueprint, "files found" no longer implies
	// "Blueprints checked" — the two need to be tracked separately instead of assumed equal.
	int32 BlueprintsChecked = 0;
	int32 NonBlueprintAssetsSkipped = 0;

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
		// surfaces here as a load failure, not as a silently-wrong class. But a null load here is not
		// automatically that: VIT1 content authoring (776e8441) put non-Blueprint DataAssets (DA_Vitals_Enemy*)
		// in this same folder by convention, and a DataAsset instance has no _C generated class to load — that
		// miss is out of scope for this test, not a redirect break. Tell the two apart by loading the ASSET
		// itself (no _C suffix) and checking what it actually is before deciding whether this is an error.
		if (!LoadedClass)
		{
			const FString AssetObjectPath = FString::Printf(TEXT("/Game/%s.%s"), *NoExt, *AssetName);
			UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetObjectPath);
			if (LoadedAsset && !Cast<UBlueprint>(LoadedAsset))
			{
				// Loads fine and isn't a Blueprint (e.g. a UDataAsset sharing this folder by convention) — this
				// test has nothing to check on it. Skip quietly; counted so the vacuous-pass guard after the
				// loop can still catch the case where EVERY found file ends up skipped this way.
				++NonBlueprintAssetsSkipped;
				continue;
			}

			// Either the asset itself failed to load (deleted/moved without a redirect), or it loaded AND is a
			// UBlueprint but its generated class (_C) still came back null — both are exactly the CoreRedirect
			// failure this test exists to catch, so this stays a hard failure, not a skip.
			AddError(FString::Printf(TEXT("%s (%s): CoreRedirect broken — %s"), *AssetName, *ObjectPath,
				LoadedAsset ? TEXT("asset loads and is a Blueprint, but its generated class (_C) did not")
							: TEXT("asset itself failed to load")));
			continue;
		}

		++BlueprintsChecked;

		// (b) Proves the reparent actually landed ON THE NEW TIER AXIS, not just "loads as something".
		TestTrue(FString::Printf(TEXT("%s reparented onto AFPSREnemyBase"), *AssetName),
			LoadedClass->IsChildOf(AFPSREnemyBase::StaticClass()));

		UE_LOG(LogFPSR, Log, TEXT("[Test] BlueprintParent: %s -> parent %s"), *AssetName,
			LoadedClass->GetSuperClass() ? *LoadedClass->GetSuperClass()->GetName() : TEXT("<none>"));

		// (c) P1 regression guard: promoting the 900 StopDistance default into AFPSREnemyBase (ADR 0013 C1) must
		// not have left a reparented BP with no stop distance at all. Deliberately NOT an equality assertion: the two
		// shipping BPs legitimately differ (one authors its own StopDistance, one inherits the promoted 900), so there
		// is no single right number to assert. ">0" is therefore a floor, not a "the value is still correct" check —
		// the VALUE is logged below so a reviewer can read back what each BP actually resolved to.
		const AFPSREnemyBase* Cdo = Cast<AFPSREnemyBase>(LoadedClass->GetDefaultObject());
		if (!Cdo)
		{
			AddError(FString::Printf(TEXT("%s loaded but its CDO is not an AFPSREnemyBase"), *AssetName));
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s CDO GetStopDistance() > 0"), *AssetName), Cdo->GetStopDistance() > 0.0f);

		// (d) The check with real teeth for CONTENT: a class redirect can succeed (loads fine, parent correct) while a
		// tagged property still drops to its default — and ProjectileClass dropping to null is silent catastrophe:
		// AFPSREnemyBase::FireProjectile just logs once and returns, so EVERY enemy stops shooting with no crash, no
		// failed test, and no obviously-broken frame. Nothing else in the automated gate covers this. Read through
		// reflection because the property is protected (a test must not force production code to widen its access).
		if (const FClassProperty* ProjectileProp = FindFProperty<FClassProperty>(LoadedClass, TEXT("ProjectileClass")))
		{
			TestNotNull(FString::Printf(TEXT("%s CDO ProjectileClass survived the reparent"), *AssetName),
				ProjectileProp->GetObjectPropertyValue_InContainer(Cdo));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s: ProjectileClass property not found by reflection — renamed or removed?"), *AssetName));
		}

		// (e) The OTHER redirect these assets depend on: DefaultEngine.ini maps the deleted
		// FPSREnemyAnimProfile_VAT_CPD onto FPSREnemyAnimProfile_Proc, and BP_EnemyRangedBase still loads through it.
		// A null AnimProfile is LEGAL (it means the whole cosmetic anim path stays dormant), so this is logged rather
		// than asserted — but if that redirect ever stops firing, the class name here changes or goes empty, and this
		// line is the only place that would say so before someone opens the editor.
		FString AnimProfileClassName = TEXT("<null>");
		if (const FObjectProperty* AnimProp = FindFProperty<FObjectProperty>(LoadedClass, TEXT("AnimProfile")))
		{
			if (const UObject* Profile = AnimProp->GetObjectPropertyValue_InContainer(Cdo))
			{
				AnimProfileClassName = Profile->GetClass()->GetName();
			}
		}

		UE_LOG(LogFPSR, Log, TEXT("[Test] BlueprintParent: %s StopDistance=%.1f AnimProfile=%s"),
			*AssetName, Cdo->GetStopDistance(), *AnimProfileClassName);

		// (f) HB1 §12-4: total UWidgetComponent count (CDO native + SCS) must be exactly 1 — never 0 (silently
		// missing, the whole point of HB1 §2's "왜 지금 구조가 안 되는가") and never 2 (native + a leftover
		// hand-authored one). CDO-only, no SpawnActor (mirrors this file's / FPSREnemyDormantPoolTest's worldless
		// convention, and SFPSRBlockoutTab.cpp's identical CDO-native + SCS-walk pattern for the same reason): the
		// CDO already carries every NATIVE default subobject from the full C++ ctor chain
		// (AFPSREnemyBase::HealthBarWidgetComponent included, however many native levels deep), and a Blueprint's
		// OWN SimpleConstructionScript nodes are walked directly by reflection for anything hand-authored on top —
		// walking the FULL class chain (not just LoadedClass itself) so a future BP-to-BP inheritance level is
		// still counted, since each SCS only holds the nodes IT added, not inherited ones.
		//
		// 🔴 EXPECTED TO FAIL for BP_EnemyMeleeBase until the user completes 사용자 작업 2 (HB1 §11) — its manual
		// SCS-authored "HealthBarWidget" component still coexists with the new native one until removed by hand.
		// That failure is the point (a mechanized "유닛 완결 조건", not an implementation defect) — see this
		// session's report.
		int32 WidgetComponentCount = 0;

		TInlineComponentArray<UWidgetComponent*> NativeWidgetComponents;
		Cdo->GetComponents(NativeWidgetComponents);
		WidgetComponentCount += NativeWidgetComponents.Num();

		for (const UClass* Class = LoadedClass; Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Class);
			if (!BPGC || !BPGC->SimpleConstructionScript)
			{
				continue;
			}
			for (const USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Cast<UWidgetComponent>(Node->ComponentTemplate))
				{
					++WidgetComponentCount;
				}
			}
		}

		TestEqual(FString::Printf(TEXT("%s has exactly 1 UWidgetComponent total (native CDO + SCS)"), *AssetName),
			WidgetComponentCount, 1);
		UE_LOG(LogFPSR, Log, TEXT("[Test] BlueprintParent: %s UWidgetComponent count (native+SCS) = %d"),
			*AssetName, WidgetComponentCount);
	}

	// Report the COVERAGE, not just the verdict (same principle as the file-count log above, one level
	// deeper): once check (a) can legitimately skip a found file as non-Blueprint content, "N files found"
	// no longer implies "N Blueprints checked" — log what was actually checked vs. skipped so a reviewer can
	// read back real coverage, not just the upfront file count.
	UE_LOG(LogFPSR, Log, TEXT("[Test] BlueprintParent: %d Blueprint(s) checked, %d non-Blueprint asset(s) skipped (of %d file(s) found)"),
		BlueprintsChecked, NonBlueprintAssetsSkipped, FoundFiles.Num());

	// Vacuous-pass guard #2 (required, not optional — same reasoning as the zero-files guard above, one level
	// deeper): the skip branch added in check (a) opens a new way to pass vacuously that the file-count guard
	// can't see. If every enemy Blueprint were renamed out of this folder or deleted while only DataAssets
	// remained, every found file would take the skip path, BlueprintsChecked would stay 0, and the function
	// would still fall through to "return true" with nothing actually verified.
	if (BlueprintsChecked == 0)
	{
		AddError(FString::Printf(TEXT("Found %d .uasset file(s) under %s but 0 were Blueprints — the scan is broken, or every enemy Blueprint has left this folder"), FoundFiles.Num(), *EnemyDir));
		return false;
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
