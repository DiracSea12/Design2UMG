// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "FontResolver.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGTextWidgetBuilder
{
	struct FIconFontSettingsSnapshot;
}

namespace Design2UMGNodeWidgetFactory
{
	struct FBuildArgs
	{
		const FDesign2UMGFontMappings* FontMappings = nullptr;
		const TSet<FString>* ExistingWarnedFamilies = nullptr;
		const Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot* IconFontSettingsSnapshot = nullptr;
	};

	struct FBuildResult
	{
		FUMGWidgetNode Widget;
		TArray<FString> Warnings;
		TSet<FString> NewlyWarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TMap<FString, FString> PendingIconFontMappings;
	};

	FBuildResult CreateWidgetForNodeType(
		const TSharedPtr<FDesignNode>& Node,
		const FBuildArgs& Args = FBuildArgs());
	void PopulateWidgetIdentityAndBounds(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
}
