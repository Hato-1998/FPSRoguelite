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
			"PropertyEditor",          // IDetailsView (P1 Data Editor — reuse the engine's property editing UI)
			"WorkspaceMenuStructure",  // WorkspaceMenu::GetMenuStructure().GetToolsCategory() for the nomad tab group
			"InputCore"                // EKeys::* referenced by SListView/SComboBox/SNumericEntryBox key-nav (link dep)
		});
	}
}
