// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImageToUMGEditor : ModuleRules
{
	public ImageToUMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"PropertyEditor",
			"UnrealEd",
			"UMGEditor",
			"Blutility",
			"AssetTools",
			"ContentBrowser",
			"ContentBrowserData",
			"ToolMenus",
			"HTTP",
			"Json",
			"JsonUtilities",
			"BlueprintGraph",
			"Kismet",
			"InputCore",
			"Projects",
			"DeveloperSettings",
			"ImageWrapper",
			"EditorSubsystem",
			"EditorFramework",
			"ApplicationCore",
			"RenderCore",
		});
	}
}
