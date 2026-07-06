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
			"Projects"
		});
	}
}
