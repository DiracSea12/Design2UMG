// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SSPDX-License-Identifier: MIT

using UnrealBuildTool;

public class Design2UMGEditor : ModuleRules
{
	public Design2UMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Design2UMGCore",
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
			"MovieScene",
			"MovieSceneTracks",
			"ImageWrapper",
			"HTTP",
			"RenderCore",
			"RHI",
			"ToolMenus",
			"DesktopPlatform",
			"Sockets",
			"Networking",
			"ContentBrowser",
		});
	}
}


