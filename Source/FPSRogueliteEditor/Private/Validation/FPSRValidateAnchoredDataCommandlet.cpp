// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRValidateAnchoredDataCommandlet.h"
#include "Validation/FPSRAnchoredValidationService.h"

#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRValidateAnchoredData, Log, All);

int32 UFPSRValidateAnchoredDataCommandlet::Main(const FString& Params)
{
	// This commandlet drives UEditorValidatorSubsystem, which only exists on GEditor (matches the engine's own
	// UDataValidationCommandlet::ValidateData pattern — see Engine/Plugins/Editor/DataValidation).
	check(GEditor);

	UE_LOG(LogFPSRValidateAnchoredData, Log, TEXT("--------------------------------------------------------------------------------------------"));
	UE_LOG(LogFPSRValidateAnchoredData, Log, TEXT("FPSRValidateAnchoredData: starting"));

	// Make sure the asset registry has actually scanned Content/ before we ask it for anchors — a commandlet run
	// with an async/partial registry would silently see zero anchors and pass an empty validation.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	const TArray<FAssetData> Anchors = FFPSRAnchoredValidationService::FindAnchorAssets();
	if (Anchors.Num() == 0)
	{
		// False-green guard: zero anchors almost certainly means the anchor classes didn't resolve (module not
		// loaded, wrong class path) or Content/ genuinely has no card pool / run schedule / loadout pool yet — either
		// way, silently reporting "0 invalid" would be a lie about coverage. Fail loudly instead.
		UE_LOG(LogFPSRValidateAnchoredData, Error, TEXT("FPSRValidateAnchoredData: found ZERO anchor assets (UFPSRCardPoolDataAsset / UFPSRRunScheduleDataAsset / UFPSRLoadoutPoolDataAsset). Refusing to report success with no coverage."));
		return 1;
	}

	const TArray<FAssetData> ToValidate = FFPSRAnchoredValidationService::GatherAssetsToValidate();

	UEditorValidatorSubsystem* EditorValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	check(EditorValidationSubsystem);

	FValidateAssetsSettings Settings;
	Settings.bSkipExcludedDirectories = true;
	Settings.bShowIfNoFailures = true;
	Settings.ValidationUsecase = EDataValidationUsecase::Commandlet;

	FValidateAssetsResults Results;
	EditorValidationSubsystem->ValidateAssetsWithSettings(ToValidate, Settings, Results);

	const int32 InvalidCount = Results.NumInvalid;

	// Orphan pass: reachable-from-anchor content is the only thing that gates the build; unreachable leaf assets
	// (abandoned drafts, renamed-off content) are surfaced as warnings so designers can clean them up on their own
	// schedule, never as a failure.
	const TArray<FAssetData> Orphans = FFPSRAnchoredValidationService::FindOrphans();
	for (const FAssetData& Orphan : Orphans)
	{
		UE_LOG(LogFPSRValidateAnchoredData, Warning, TEXT("FPSRValidateAnchoredData: orphan (unreachable from any anchor) — %s"), *Orphan.GetObjectPathString());
	}

	UE_LOG(LogFPSRValidateAnchoredData, Log, TEXT("FPSRValidateAnchoredData: anchors=%d validated=%d invalid=%d orphans=%d"),
		Anchors.Num(), ToValidate.Num(), InvalidCount, Orphans.Num());
	UE_LOG(LogFPSRValidateAnchoredData, Log, TEXT("--------------------------------------------------------------------------------------------"));

	return InvalidCount > 0 ? 1 : 0;
}
