// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGNodeWidgetFactory
{
	struct FBuildContext
	{
		FBuildContext(
			TArray<FString>& InWarnings,
			TSet<FString>& InWarnedFamilies,
			TSet<FString>& InPrunedInvalidMappings,
			TMap<FString, FString>& InPendingIconFontMappings)
			: Warnings(InWarnings)
			, WarnedFamilies(InWarnedFamilies)
			, PrunedInvalidMappings(InPrunedInvalidMappings)
			, PendingIconFontMappings(InPendingIconFontMappings)
		{
		}

		TArray<FString>& Warnings;
		TSet<FString>& WarnedFamilies;
		TSet<FString>& PrunedInvalidMappings;
		TMap<FString, FString>& PendingIconFontMappings;
	};

	FUMGWidgetNode CreateWidgetForNodeType(const TSharedPtr<FDesignNode>& Node, FBuildContext& Context);
	void PopulateWidgetIdentityAndBounds(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
}
