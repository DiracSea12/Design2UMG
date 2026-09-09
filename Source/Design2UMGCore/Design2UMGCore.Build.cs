// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class Design2UMGCore : ModuleRules
{
	public Design2UMGCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"SlateCore",
			"Json",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"Slate",
			"UnrealEd",
		});

		if (Target.bBuildDeveloperTools)
		{
			PrivateDependencyModuleNames.Add("Projects");
		}
	}
}
