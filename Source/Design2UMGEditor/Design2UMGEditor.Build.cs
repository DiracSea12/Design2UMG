// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class Design2UMGEditor : ModuleRules
{
	public Design2UMGEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The plugin compiles against precompiled engines too; retain headers and
		// static-library dependencies that the engine-only helper omits in that mode.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"FreeType2", "libcurl", "OpenSSL", "UElibPNG", "zlib"
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 0)
		{
			// UE 5.0 keeps WidgetBlueprintEditorUtils in UMGEditor's private headers.
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source/Editor/UMGEditor/Private"));
		}
#if UE_5_1_OR_LATER
		PrivateDependencyModuleNames.Add("nghttp2");
#endif

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
			"SSL",
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
