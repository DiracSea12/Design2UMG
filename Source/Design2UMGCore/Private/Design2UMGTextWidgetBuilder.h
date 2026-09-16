// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "FontResolver.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGTextWidgetBuilder
{
	struct FIconFontSettingsSnapshot
	{
		TMap<FString, FString> IconFontAssetPaths;
		FString DefaultIconFontAssetPath;
		FString MissingIconFontPlaceholder;

		FString FindIconFontAssetPath(const FString& FontFamily) const;
	};

	struct FIconFontBuildArgs
	{
		const FDesign2UMGFontMappings* FontMappings = nullptr;
		const TSet<FString>* ExistingWarnedFamilies = nullptr;
		const FIconFontSettingsSnapshot* SettingsSnapshot = nullptr;
	};

	struct FIconFontBuildResult
	{
		FUMGWidgetNode Widget;
		TArray<FString> Warnings;
		TSet<FString> NewlyWarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TMap<FString, FString> PendingIconFontMappings;
		bool bMissingSettingsSnapshot = false;
	};

	FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node, const FDesign2UMGFontMappings* FontMappings = nullptr);
	FIconFontBuildResult CreateIconFontWidget(
		const TSharedPtr<FDesignNode>& Node,
		const FIconFontBuildArgs& Args = FIconFontBuildArgs());
}

