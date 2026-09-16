// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace FigmaComponentDependencies
{
	/** Adapt a current REST file response into the same explicit dependency bundle as the exporter. */
	DESIGN2UMGCORE_API bool PrepareRestFile(const FString& Json, const FString& FileKey, FString& OutJson, FString& OutError);
	/** Keep only the selected import root and dependency evidence originating in that subtree. */
	DESIGN2UMGCORE_API bool FilterRestFileToSelectedRoot(
		const TSharedPtr<FJsonObject>& Root,
		const FString& SelectedRootId,
		FString& OutError);
	struct FReference
	{
		FString Key;
		/** Resolved nested instances, including swaps. An explicit WBP mapping stops this entire branch. */
		TArray<FReference> Children;
	};

	struct FDefinition
	{
		FString NodeId;
		FString Key;
		FString ComponentSetId;
		FString ComponentSetKey;
		TSharedPtr<FJsonObject> Document;
		TArray<FReference> References;
	};

	struct FSourceError
	{
		FString Kind;
		FString NodeId;
		FString Message;
	};

	struct FCatalog
	{
		TMap<FString, FDefinition> Definitions;
		TMap<FString, FString> InstanceKeys;
		TArray<FSourceError> Errors;
	};

	/** Reads the exporter bundle, including failures. A parsed bundle is not a successful import. */
	DESIGN2UMGCORE_API bool Parse(const FString& Json, FCatalog& OutCatalog, FString& OutError);
	/** Requested keys only; explicit project mappings terminate traversal. No asset writes. */
	DESIGN2UMGCORE_API bool BuildOrder(const FCatalog& Catalog, const TArray<FString>& RequestedKeys,
		const TSet<FString>& ExistingWidgetKeys, TArray<FString>& OutOrder, FString& OutError);
}
