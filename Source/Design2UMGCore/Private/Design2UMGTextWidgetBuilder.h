// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGTextWidgetBuilder
{
	struct FIconFontBuildContext
	{
		FIconFontBuildContext(
			TSet<FString>& InWarnedFamilies,
			TSet<FString>& InPrunedInvalidMappings,
			TMap<FString, FString>& InPendingIconFontMappings)
			: WarnedFamilies(InWarnedFamilies)
			, PrunedInvalidMappings(InPrunedInvalidMappings)
			, PendingIconFontMappings(InPendingIconFontMappings)
		{
		}

		TSet<FString>& WarnedFamilies;
		TSet<FString>& PrunedInvalidMappings;
		TMap<FString, FString>& PendingIconFontMappings;
	};

	FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node, FIconFontBuildContext& Context);
}

