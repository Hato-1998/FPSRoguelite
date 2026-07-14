// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSRogueliteEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Validation/FPSRAnchoredValidationService.h"
#include "DataEditor/SFPSRDataEditorWidget.h"
#include "Assembler/SFPSRWeaponAssemblerTab.h"
#include "Blockout/SFPSRBlockoutTab.h"
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
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FPSRogueliteEditorModule"

const FName FFPSRogueliteEditorModule::FPSRDataEditorTabName(TEXT("FPSRDataEditor"));
const FName FFPSRogueliteEditorModule::FPSRWeaponAssemblerTabName(TEXT("FPSRWeaponAssembler"));
const FName FFPSRogueliteEditorModule::FPSRBlockoutTabName(TEXT("FPSRBlockout"));

// UEditorValidatorBase subclasses (UFPSRCardPoolValidator / UFPSRRunScheduleValidator / UFPSRLoadoutPoolValidator)
// are auto-discovered by UEditorValidatorSubsystem — no manual registration needed here. This module only wires up
// the Tools > FPSR menu entry point, matching the engine's own DataValidation module pattern (RegisterStartupCallback
// deferred until menus are ready; UToolMenus::UnregisterOwner by `this` on shutdown).
void FFPSRogueliteEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFPSRogueliteEditorModule::RegisterMenus));

	// FPSR Data Editor (P1) nomad tab — same lifetime pattern as the engine's own tool tabs (register at startup,
	// unregister at shutdown). Nomad = can be docked anywhere, not tied to a specific asset editor's tab layout.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FPSRDataEditorTabName, FOnSpawnTab::CreateStatic(&FFPSRogueliteEditorModule::SpawnDataEditorTab))
		.SetDisplayName(LOCTEXT("DataEditorTabTitle", "FPSR 데이터 에디터"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	// Weapon Part Assembler — fully embedded-viewport tool tab (weapon DA picker + own 3D preview + parts list +
	// gizmo + bake-to-socket button). Same lifetime pattern as the Data Editor tab above.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FPSRWeaponAssemblerTabName, FOnSpawnTab::CreateStatic(&FFPSRogueliteEditorModule::SpawnWeaponAssemblerTab))
		.SetDisplayName(LOCTEXT("WeaponAssemblerTabTitle", "무기 파츠 조립기"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	// FPSR Blockout tool — config-driven modular map palette (Slice ① = empty shell + UDeveloperSettings backbone).
	// Same nomad-tab lifetime pattern as the two tabs above.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FPSRBlockoutTabName, FOnSpawnTab::CreateStatic(&FFPSRogueliteEditorModule::SpawnBlockoutTab))
		.SetDisplayName(LOCTEXT("BlockoutTabTitle", "블록아웃 툴"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void FFPSRogueliteEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FPSRDataEditorTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FPSRWeaponAssemblerTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FPSRBlockoutTabName);
}

void FFPSRogueliteEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("FPSR"), LOCTEXT("FPSRSection", "FPSR"));
	Section.AddMenuEntry(
		"FPSRValidateAnchoredData",
		LOCTEXT("ValidateAnchoredDataTitle", "앵커 데이터 검증"),
		LOCTEXT("ValidateAnchoredDataTooltip", "카드 풀 / 런 스케줄 / 로드아웃 풀 앵커와 그로부터 도달 가능한 모든 것에 데이터 검증을 실행합니다 (Content/ 하위 전체 에셋이 아님)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnValidateAnchoredDataMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSROpenDataEditor",
		LOCTEXT("OpenDataEditorTitle", "데이터 에디터…"),
		LOCTEXT("OpenDataEditorTooltip", "FPSR 데이터 에디터 열기 (배선 + 카드 매그니튜드 + 스케줄 미리보기)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnOpenDataEditorMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSROpenWeaponAssembler",
		LOCTEXT("OpenWeaponAssemblerTitle", "무기 파츠 조립기…"),
		LOCTEXT("OpenWeaponAssemblerTooltip", "무기 파츠 조립기 열기 (무기 DA 선택 + 3D 프리뷰 + 기즈모 배치 + 소켓 굽기·저장)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnOpenWeaponAssemblerMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSROpenBlockout",
		LOCTEXT("OpenBlockoutTitle", "블록아웃 툴…"),
		LOCTEXT("OpenBlockoutTooltip", "FPSR 블록아웃 툴 열기 (config 기반 모듈러 맵 팔레트 + 블록아웃 가드레일). 팔레트 폴더는 Project Settings > FPSR > FPSR Blockout 에서 설정."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnOpenBlockoutMenuEntry))
	);
}

void FFPSRogueliteEditorModule::OnOpenDataEditorMenuEntry()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FPSRDataEditorTabName);
}

TSharedRef<SDockTab> FFPSRogueliteEditorModule::SpawnDataEditorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SFPSRDataEditorWidget)
		];
}

void FFPSRogueliteEditorModule::OnOpenWeaponAssemblerMenuEntry()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FPSRWeaponAssemblerTabName);
}

TSharedRef<SDockTab> FFPSRogueliteEditorModule::SpawnWeaponAssemblerTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SFPSRWeaponAssemblerTab)
		];
}

void FFPSRogueliteEditorModule::OnOpenBlockoutMenuEntry()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FPSRBlockoutTabName);
}

TSharedRef<SDockTab> FFPSRogueliteEditorModule::SpawnBlockoutTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SFPSRBlockoutTab)
		];
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
