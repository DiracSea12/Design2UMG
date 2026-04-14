// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGPrototypeNavigation
{
	struct FPrototypeNavigationAnalysis
	{
		FUMGPrototypeNavigationData Data;
		TSet<FString> TriggerNodeIds;
	};

	FPrototypeNavigationAnalysis BuildAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings);

	bool ShouldWrapNodeWithButton(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGPrototypeNavigationData& NavigationData,
		const TSet<FString>& TriggerNodeIds);

	FUMGWidgetNode WrapWithButton(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node);

	FUMGWidgetNode BuildRoot(
		TArray<FUMGWidgetNode>&& PageWidgets,
		const FUMGPrototypeNavigationData& NavigationData);
}
