// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSRogueliteEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Validation/FPSRAnchoredValidationService.h"
#include "DataEditor/SFPSRDataEditorWidget.h"
#include "Assembler/SFPSRWeaponAssemblerTab.h"
#include "Assembler/SFPSRWeaponAssemblerFPTab.h"
#include "Blockout/SFPSRBlockoutTab.h"
#include "Arena/FPSRArenaAuthoringTool.h"
#include "Localization/FPSRStringTableReload.h"
#include "CardImport/FPSRCardCsvExporter.h"
#include "CardImport/FPSRCardCsvImporter.h"
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
#include "Misc/ScopedSlowTask.h"
#include "Misc/Paths.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FPSRogueliteEditorModule"

const FName FFPSRogueliteEditorModule::FPSRDataEditorTabName(TEXT("FPSRDataEditor"));
const FName FFPSRogueliteEditorModule::FPSRWeaponAssemblerTabName(TEXT("FPSRWeaponAssembler"));
const FName FFPSRogueliteEditorModule::FPSRBlockoutTabName(TEXT("FPSRBlockout"));
const FName FFPSRogueliteEditorModule::FPSRWeaponAssemblerFPTabName(TEXT("FPSRWeaponAssemblerFP"));

TWeakPtr<SFPSRWeaponAssemblerTab> FFPSRogueliteEditorModule::LiveWeaponAssemblerTab;

FName FFPSRogueliteEditorModule::GetWeaponAssemblerFPTabName()
{
	return FPSRWeaponAssemblerFPTabName;
}

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

	// Weapon Assembler — first-person view. A SECOND tab on purpose: a docked viewport's aspect ratio is whatever the
	// layout gives it, so the vertical framing differs from the game even at the same FOV (measured: 82.3° vs 58.7°).
	// Its own tab can be floated/resized freely and pins the aspect with letterboxing.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FPSRWeaponAssemblerFPTabName, FOnSpawnTab::CreateStatic(&FFPSRogueliteEditorModule::SpawnWeaponAssemblerFPTab))
		.SetDisplayName(LOCTEXT("WeaponAssemblerFPTabTitle", "무기 1인칭 뷰"))
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
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FPSRWeaponAssemblerFPTabName);
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
		"FPSROpenWeaponAssemblerFP",
		LOCTEXT("OpenWeaponAssemblerFPTitle", "무기 1인칭 뷰…"),
		LOCTEXT("OpenWeaponAssemblerFPTooltip", "무기 파츠 조립기의 프리뷰를 '플레이어 눈' 시점 + 게임과 같은 화면비로 보여주는 창을 엽니다. 조립기 탭에서 그립을 만지면 이 창에 바로 반영됩니다 (조립기 탭이 먼저 열려 있어야 합니다)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnOpenWeaponAssemblerFPMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSROpenBlockout",
		LOCTEXT("OpenBlockoutTitle", "블록아웃 툴…"),
		LOCTEXT("OpenBlockoutTooltip", "FPSR 블록아웃 툴 열기 (config 기반 모듈러 맵 팔레트 + 블록아웃 가드레일). 팔레트 폴더는 Project Settings > FPSR > FPSR Blockout 에서 설정."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnOpenBlockoutMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSRArenaProposeLayout",
		LOCTEXT("ArenaProposeLayoutTitle", "아레나 시작 배치 제안"),
		LOCTEXT("ArenaProposeLayoutTooltip", "레벨의 FPSRArenaActor 파라미터와 '시작 시드'로 클러스터 배치를 제안해 FPSRArenaBlocker 를 놓습니다. 순환·교차·최소 통로폭이 보장된 시작점이며, 손대는 순간부터는 '아레나 검증'이 책임집니다. 기존 블로커가 있으면 교체 여부를 묻고, 전체가 한 번의 Ctrl+Z 로 되돌아갑니다."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRArenaAuthoringTool::ProposeStartingLayout))
	);
	Section.AddMenuEntry(
		"FPSRArenaBake",
		LOCTEXT("ArenaBakeTitle", "아레나 베이크"),
		LOCTEXT("ArenaBakeTooltip", "레벨에 놓인 지오메트리의 콜리전(WorldStatic + Query)에서 적 이동 마스크를 구워 각 아레나의 '베이크 데이터' 에셋에 저장합니다. 런타임은 이 결과만 읽습니다 — 절차 생성도 월드 트레이스도 하지 않습니다 (ADR 0012). 셀 예산을 넘으면 조용히 성기게 굽는 대신 거부합니다. 에셋은 더티로 두므로 Ctrl+S 로 저장하세요."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRArenaAuthoringTool::BakeArenasInLevel))
	);
	Section.AddMenuEntry(
		"FPSRArenaValidate",
		LOCTEXT("ArenaValidateTitle", "아레나 검증"),
		LOCTEXT("ArenaValidateTooltip", "레벨에 배치된 블로커·랜드마크를 실제 런타임과 같은 경로로 셀 마스크로 굽고 검사합니다: 단일 연결성분 · 순환 회로 · 최소 통로폭 · 오목 주머니 · 프롭 여유분 (ADR 0011 E4)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRArenaAuthoringTool::ValidateArenaInLevel))
	);
	Section.AddMenuEntry(
		"FPSRReloadStringTableCsv",
		LOCTEXT("ReloadStringTableCsvTitle", "StringTable CSV 리로드"),
		LOCTEXT("ReloadStringTableCsvTooltip", "Content/StringTables/*.csv 3개를 다시 읽어 런타임 문자열 테이블을 재등록합니다 (에디터 재시작 불필요)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnReloadStringTableCsvMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSRExportCardCsv",
		LOCTEXT("ExportCardCsvTitle", "카드 CSV 내보내기(마이그레이션)"),
		LOCTEXT("ExportCardCsvTooltip", "기존 DA_Card_* 에셋 전수를 Content/Authoring/Cards.csv·CardCatalog.csv로 역추출합니다 (마이그레이션 1회용 — §2-3-10)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnExportCardCsvMenuEntry))
	);
	Section.AddMenuEntry(
		"FPSRImportCardCsv",
		LOCTEXT("ImportCardCsvTitle", "카드 CSV 임포트"),
		LOCTEXT("ImportCardCsvTooltip", "Content/Authoring/Cards.csv·CardCatalog.csv를 읽어 DA_Card_*를 생성/갱신하고 검증 통과분을 저장합니다 (§2-3-10)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "DeveloperTools.MenuIcon"),
		FUIAction(FExecuteAction::CreateStatic(&FFPSRogueliteEditorModule::OnImportCardCsvMenuEntry))
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
	// 1인칭 뷰 탭이 이 위젯의 프리뷰 씬을 찾아갈 수 있도록 등록해 둔다(약참조 — 헤더의 GetLiveWeaponAssemblerTab 주석).
	// 재열기면 여기서 새 위젯으로 갈리고, 1인칭 탭은 Tick 에서 씬이 바뀐 걸 감지해 렌더를 멈춘다.
	TSharedRef<SFPSRWeaponAssemblerTab> AssemblerWidget = SNew(SFPSRWeaponAssemblerTab);
	LiveWeaponAssemblerTab = AssemblerWidget;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			AssemblerWidget
		];
}

void FFPSRogueliteEditorModule::OnOpenWeaponAssemblerFPMenuEntry()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FPSRWeaponAssemblerFPTabName);
}

TSharedRef<SDockTab> FFPSRogueliteEditorModule::SpawnWeaponAssemblerFPTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SFPSRWeaponAssemblerFPTab)
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

void FFPSRogueliteEditorModule::OnReloadStringTableCsvMenuEntry()
{
	// ReloadAll already logs a per-table Error line + summary and, on partial failure, raises its own toast
	// notification — nothing else to surface here.
	FPSRStringTableReload::ReloadAll();
}

void FFPSRogueliteEditorModule::OnExportCardCsvMenuEntry()
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ExportCardCsvSlowTask", "카드 CSV 내보내는 중..."));
	SlowTask.MakeDialog();

	const FString CardsPath = FPaths::ProjectContentDir() / TEXT("Authoring/Cards.csv");
	const FString CatalogPath = FPaths::ProjectContentDir() / TEXT("Authoring/CardCatalog.csv");

	TArray<FString> Errors;
	const bool bSucceeded = FPSRCardCsvExport::ExportAll(CardsPath, CatalogPath, Errors);
	SlowTask.EnterProgressFrame(1.0f);

	FMessageLog ExportLog(TEXT("FPSRCardCsvExport"));
	if (bSucceeded)
	{
		ExportLog.Info(FText::Format(LOCTEXT("ExportCardCsvSucceeded", "카드 CSV 내보내기 완료: {0}, {1}"), FText::FromString(CardsPath), FText::FromString(CatalogPath)));
	}
	else
	{
		ExportLog.Error(LOCTEXT("ExportCardCsvFailed", "카드 CSV 내보내기 실패 — 아래 오류를 확인하세요. 파일은 기록되지 않았습니다."));
	}
	for (const FString& Error : Errors)
	{
		ExportLog.Error(FText::FromString(Error));
	}
	ExportLog.Open(bSucceeded ? EMessageSeverity::Info : EMessageSeverity::Error, /*bOpenEvenIfEmpty=*/true);
}

void FFPSRogueliteEditorModule::OnImportCardCsvMenuEntry()
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ImportCardCsvSlowTask", "카드 CSV 임포트 중..."));
	SlowTask.MakeDialog();

	const FFPSRCardImportResult Result = FPSRCardCsvImport::ImportAll(/*bSaveAssets=*/true);
	SlowTask.EnterProgressFrame(1.0f);

	FMessageLog ImportLog(TEXT("FPSRCardCsvImport"));
	ImportLog.Info(FText::Format(
		LOCTEXT("ImportCardCsvSummary", "카드 CSV 임포트: 생성 {0} / 갱신 {1} / 무변경 {2} / 풀·무기 소속 제거 {3} / 오류 {4}"),
		FText::AsNumber(Result.CreatedCount), FText::AsNumber(Result.UpdatedCount), FText::AsNumber(Result.UnchangedCount),
		FText::AsNumber(Result.RemovedMembershipCount), FText::AsNumber(Result.Errors.Num())));
	for (const FString& Error : Result.Errors)
	{
		ImportLog.Error(FText::FromString(Error));
	}
	ImportLog.Open(Result.Succeeded() ? EMessageSeverity::Info : EMessageSeverity::Error, /*bOpenEvenIfEmpty=*/true);
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
