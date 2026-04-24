// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

class UDesign2UMGSettings;

namespace Design2UMGTextWidgetBuilder
{
	struct FIconFontSettingsSnapshot
	{
		TMap<FString, FString> IconFontAssetPaths;
		FString DefaultIconFontAssetPath;
		FString MissingIconFontPlaceholder;

		FString FindIconFontAssetPath(const FString& FontFamily) const;
	};

	FIconFontSettingsSnapshot BuildIconFontSettingsSnapshot(const UDesign2UMGSettings* Settings = nullptr);

	struct FIconFontBuildArgs
	{
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

	FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);
	FIconFontBuildResult CreateIconFontWidget(
		const TSharedPtr<FDesignNode>& Node,
		const FIconFontBuildArgs& Args = FIconFontBuildArgs());
}

