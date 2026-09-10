// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class Design2UMGEditor : ModuleRules
{
	public Design2UMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public dependencies are limited to types exposed by Public/*.h:
		// module entrypoint, preview contracts, UUserWidget surfaces, and
		// Design2UMGCore data contracts. Preview Slate widgets and connector
		// clients live under Private/.
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
			"EditorSubsystem",
			"UMGEditor",
			"Blutility",
			"AssetTools",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"BlueprintGraph",
			"Kismet",
			"InputCore",
			// Standard-asset transition animations (UWidgetAnimation + UMovieScene subobjects with
			// float/2D-transform tracks) for wired Navigate bindings.
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
