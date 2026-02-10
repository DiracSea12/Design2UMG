// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PenToUMGEditor : ModuleRules
{
	public PenToUMGEditor(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd",
			"UMGEditor",
			"Blutility",
			"AssetTools",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"BlueprintGraph",
			"Kismet",
			"InputCore",
			"Projects",
			"DeveloperSettings",
			"ImageWrapper",
			"HTTP",
			"RenderCore",
			"ToolMenus",
			"DesktopPlatform",
		});
	}
}
