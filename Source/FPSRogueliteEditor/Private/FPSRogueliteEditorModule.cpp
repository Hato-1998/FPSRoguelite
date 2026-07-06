// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSRogueliteEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Validation/FPSRAnchoredValidationService.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Logging/MessageLog.h"
#include "EditorValidatorHelpers.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Styling/AppStyle.h"
#include "Misc/MessageDialog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#define LOCTEXT_NAMESPACE "FPSRogueliteEditorModule"

// UEditorValidatorBase subclasses (UFPSRCardPoolValidator / UFPSRRunScheduleValidator / UFPSRLoadoutPoolValidator)
// are auto-discovered by UEditorValidatorSubsystem — no manual registration needed here. This module only wires up
// the Tools > FPSR menu entry point, matching the engine's own DataValidation module pattern (RegisterStartupCallback
// deferred until menus are ready; UToolMenus::UnregisterOwner by `this` on shutdown).
void FFPSRogueliteEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFPSRogueliteEditorModule::RegisterMenus));
}

void FFPSRogueliteEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FFPSRogueliteEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("FPSR"), LOCTEXT("FPSRSection", "FPSR"));
	Section.AddMenuEntry(
		"FPSRValidateAnchoredData",
		LOCTEXT("ValidateAnchoredDataTitle", "Validate Anchored Data"),
		LOCTEXT("ValidateAnchoredDataTooltip", "Runs data validation on the card pool / run schedule / loadout pool anchors and everything reachable from them (not every asset under Content/)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnValidateAnchoredDataMenuEntry))
	);
}

void FFPSRogueliteEditorModule::OnValidateAnchoredDataMenuEntry()
{
	// Make sure the asset registry has actually finished scanning before we ask it for anchors (in-editor the scan
	// is async at startup; a designer clicking this moments after opening the editor could otherwise see 0 anchors).
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("StillScanning", "Cannot run data validation while the Asset Registry is still discovering assets. Try again in a moment."));
		return;
	}

	const TArray<FAssetData> Anchors = FFPSRAnchoredValidationService::FindAnchorAssets();
	if (Anchors.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoAnchors", "Found zero anchor assets (card pool / run schedule / loadout pool) — nothing to validate."));
		return;
	}

	const TArray<FAssetData> ToValidate = FFPSRAnchoredValidationService::GatherAssetsToValidate();

	UEditorValidatorSubsystem* EditorValidationSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;
	if (EditorValidationSubsystem == nullptr)
	{
		return;
	}

	FValidateAssetsSettings Settings;
	Settings.bSkipExcludedDirectories = true;
	Settings.bShowIfNoFailures = true;
	Settings.ValidationUsecase = EDataValidationUsecase::Manual;
	Settings.MessageLogPageTitle = LOCTEXT("MessageLogPageTitle", "FPSR Anchored Data Validation");

	FValidateAssetsResults Results;
	EditorValidationSubsystem->ValidateAssetsWithSettings(ToValidate, Settings, Results);

	// Orphan pass: reported into the SAME message log as a warning per orphan, so a single "AssetCheck" tab shows
	// both the pass/fail results and the dead-content list. Never fails the run — see FindOrphans' contract.
	FMessageLog AssetCheckLog(UE::DataValidation::MessageLogName);
	for (const FAssetData& Orphan : FFPSRAnchoredValidationService::FindOrphans())
	{
		AssetCheckLog.Warning(FText::Format(
			LOCTEXT("OrphanMessage", "Orphan (unreachable from any anchor): {0}"),
			FText::FromString(Orphan.GetObjectPathString())));
	}

	// ValidateAssetsWithSettings already opens/populates the "AssetCheck" message log via bShowIfNoFailures /
	// ShowMessageLogSeverity; force it open here too so the orphan warnings just added are visible even when the
	// validation pass itself reported nothing.
	AssetCheckLog.Open(EMessageSeverity::Info, /*bOpenEvenIfEmpty=*/true);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFPSRogueliteEditorModule, FPSRogueliteEditor);
