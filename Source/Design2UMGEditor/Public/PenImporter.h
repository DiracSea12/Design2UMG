// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

/**
 * Entry points for importing .pen content into widget data.
 */
class FPenImporter
{
public:
	/** Import a .pen file and convert it to UMG widget data. */
	static bool ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData);

	/** Import from a pen JSON string. */
	static bool ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData);

	/** Import a specific target node from a multi-frame .pen file. */
	static bool ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData);
};
