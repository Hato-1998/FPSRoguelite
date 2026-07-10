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
			"Slate",
			"SlateCore",
			"EditorSubsystem",
			"ToolMenus",
			"AssetRegistry",
			"DataValidation",   // UEditorValidatorBase (Engine/Plugins/Editor/DataValidation)
			"DeveloperSettings",
			"Projects",
			"PropertyEditor",          // IDetailsView (P1 Data Editor), SObjectPropertyEntryBox (Weapon Part Assembler DA picker)
			"WorkspaceMenuStructure",  // WorkspaceMenu::GetMenuStructure().GetToolsCategory() for the nomad tab group
			"InputCore",                // EKeys::* referenced by SListView/SComboBox/SNumericEntryBox key-nav (link dep)
			"ContentBrowser",           // no longer used by the Weapon Part Assembler (now an embedded-viewport tool with
			                            // its own DA picker), kept for potential future content-browser integrations
			"AdvancedPreviewScene"      // FAdvancedPreviewScene — the Weapon Part Assembler's embedded 3D preview viewport
		});
	}
}
