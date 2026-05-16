// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

/**
 * Editor-facing JSON parse bridge for preview, import, and debugging workflows.
 * Kept public for source compatibility; this is not a runtime module API promise.
 */
class FUMGJsonParser
{
public:
	/**
	 * Parse a JSON string into widget data.
	 * @param JsonString Source JSON string
	 * @param OutWidgetData Parsed widget data
	 * @param OutError Optional parse error output
	 * @return true on success
	 */
	static bool ParseJson(const FString& JsonString, FUMGWidgetData& OutWidgetData, FString* OutError = nullptr);

private:
	static bool ParseWidgetNode(const TSharedPtr<class FJsonObject>& JsonObject, FUMGWidgetNode& OutNode);
	static void ParseSlotData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGSlotData& OutSlot);
	static void ParseAnchorsData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGAnchorsData& OutAnchors);
	static void ParseAnchorData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGAnchorData& OutAnchor);
	static void ParseOffsetsData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGOffsetsData& OutOffsets);
};
