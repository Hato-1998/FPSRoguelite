// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FPSRogueliteEditor : ModuleRules
{
	public FPSRogueliteEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FPSRoguelite",   // runtime module: the DataAsset types we validate/enumerate
			"GameplayTags"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorFramework",          // FEditorModeInfo / FEditorModeID — the blockout viewport placement UEdMode
			"LevelEditor",              // GLevelEditorModeTools() — activate the placement mode from the tab button
			"Slate",
			"SlateCore",
			"EditorSubsystem",
			"ToolMenus",
			"AssetRegistry",            // DataEditor/Assembler/Blockout/Validation 전반의 에셋 열거·참조 조회
			"AssetTools",               // IAssetTools::CreateAsset — CardCsvImporter의 신규 카드 DA 생성(§2-3-10)
			"GameplayAbilities",        // TSubclassOf<UGameplayEffect> 링크 — CardCsvExporter/Importer의 CharGE 페이로드(§2-3-10)
			"DataValidation",   // UEditorValidatorBase (Engine/Plugins/Editor/DataValidation)
			"Localization",    // FLocTextHelper (LOC0 — FPSRImportCsvTranslationsCommandlet); UnrealEd only re-exposes GatherTextCommandletBase itself, not this
			"DeveloperSettings",
			"Projects",
			"PropertyEditor",          // IDetailsView (P1 Data Editor), SObjectPropertyEntryBox (Weapon Part Assembler DA picker)
			"WorkspaceMenuStructure",  // WorkspaceMenu::GetMenuStructure().GetToolsCategory() for the nomad tab group
			"InputCore",                // EKeys::* referenced by SListView/SComboBox/SNumericEntryBox key-nav (link dep)
			"AdvancedPreviewScene",     // FAdvancedPreviewScene — the Weapon Part Assembler's embedded 3D preview viewport
			"EditorWidgets",            // SEnumComboBox — 진화 단계 트리거/스탯 콤보(Assembler evolution authoring panel)
			"UMG",                      // UUserWidget::StaticClass() — 진화 단계 스코프 오버레이 위젯 BP 피커(SClassPropertyEntryBox)
			"Persona",                  // SBoneSelectionWidget — Weapon Part Assembler 손 위치 저장의 기준 뼈 선택 UI
			"PropertyPath",             // PropertyPathHelpers::Get/SetPropertyValueFromString — FPSRAssetScripting 의 EditDefaultsOnly 우회 저작 경로
			"EditorScriptingUtilities"  // UEditorAssetLibrary::LoadAsset/SaveLoadedAsset — FPSRAssetScripting (플러그인 모듈, 이미 .uproject 에서 활성; Python 쪽은 그래서 기존에도 됐음)
		});

	}
}
