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
			"GameplayTags",
			"ImGui"           // GunMotionTool_Spec.md §1/§5 — 인게임 스튜디오 렌더 컨텍스트(ImGui::FScopedContext, imgui.h/imgui_internal.h
			                  // 를 이 모듈 전체가 시스템 include 경로로 상속받도록 Public — README 사용례와 동일)
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
			"AssetRegistry",            // GunMotionTool_Spec.md §3-3/A17 — GetReferencers 몽타주 스캔(FPSRGunMotionStudioBaker)
			"DataValidation",   // UEditorValidatorBase (Engine/Plugins/Editor/DataValidation)
			"DeveloperSettings",
			"Projects",
			"PropertyEditor",          // IDetailsView (P1 Data Editor), SObjectPropertyEntryBox (Weapon Part Assembler DA picker)
			"WorkspaceMenuStructure",  // WorkspaceMenu::GetMenuStructure().GetToolsCategory() for the nomad tab group
			"InputCore",                // EKeys::* referenced by SListView/SComboBox/SNumericEntryBox key-nav (link dep)
			"AdvancedPreviewScene",     // FAdvancedPreviewScene — the Weapon Part Assembler's embedded 3D preview viewport
			"EditorWidgets",            // SEnumComboBox — 진화 단계 트리거/스탯 콤보(Assembler evolution authoring panel)
			"UMG",                      // UUserWidget::StaticClass() — 진화 단계 스코프 오버레이 위젯 BP 피커(SClassPropertyEntryBox)
			"Persona"                   // SBoneSelectionWidget — Weapon Part Assembler 손 위치 저장의 기준 뼈 선택 UI
		});

		// ImGuizmo (vendored, Private/GunMotionStudio/ImGuizmo, MIT) defines IMGUI_DEFINE_MATH_OPERATORS itself right
		// before its own first `#include "imgui.h"` — but imgui.h is `#pragma once`, so in a Unity build another .cpp
		// in this module could include it FIRST (without the macro) and silently deny ImGuizmo.cpp the ImVec2/ImVec4
		// operators it needs (whichever translation unit's include wins the pragma-once race, non-deterministically
		// across incremental builds). Defining it at the module level via PrivateDefinitions removes the ordering
		// dependency entirely — it is set before ANY inclusion in ANY .cpp of this module.
		PrivateDefinitions.Add("IMGUI_DEFINE_MATH_OPERATORS=1");
	}
}
