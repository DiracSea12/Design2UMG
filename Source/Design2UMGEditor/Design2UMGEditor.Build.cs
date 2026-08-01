// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class Design2UMGEditor : ModuleRules
{
	public Design2UMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public dependencies are limited to types exposed by Public/*.h:
		// preview Slate widgets, UUserWidget surfaces, and Design2UMGCore data contracts.
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
			"MovieScene",
			"MovieSceneTracks",
			"ImageCore",
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

		if (Target.bBuildDeveloperTools)
		{
			PrivateDependencyModuleNames.Add("Projects");
		}
	}
}
