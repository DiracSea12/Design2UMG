// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGPrototypeNavigation.h"

namespace Design2UMGPrototypeStepGraphAssembly
{
	struct FPrototypeRoutingContext
	{
		TSet<FString> ReservedTokens;
		uint32 InternalTokenHash = 0;
		TMap<FString, FString> NodeToPageId;
		TMap<FString, int32> PageIdToIndex;
		TArray<FString> PageIds;
	};

	FPrototypeRoutingContext BuildRoutingContext(
		const TArray<TSharedPtr<FDesignNode>>& PrototypePages);

	FString MakeUniquePrototypeInternalToken(
		const TSet<FString>& ReservedTokens,
		uint32 PrototypeInternalTokenHash,
		const FString& Suffix);

	void GatherNavigationBindings(
		const TSharedPtr<FDesignNode>& RootNode,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationBinding>& OutBindings,
		TSet<FString>& OutTriggerIds,
		TSet<FString>& InOutSeenBindingKeys,
		TArray<FString>& Warnings);
}