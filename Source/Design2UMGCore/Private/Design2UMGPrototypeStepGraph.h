// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGPrototypeNavigation.h"

// Step graph owner: prototype routing analysis, action parsing, step appending,
// binding gathering, and binding summary reconstruction.
namespace Design2UMGPrototypeStepGraph
{
	Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis BuildAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings);

	void GatherBindings(
		const TSharedPtr<FDesignNode>& RootNode,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationBinding>& OutBindings,
		TSet<FString>& OutTriggerIds,
		TSet<FString>& InOutSeenBindingKeys,
		TArray<FString>& Warnings);
}
