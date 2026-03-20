// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * Parses JSON payloads into normalized UMG widget data.
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
	static void ParseProperties(const TSharedPtr<class FJsonObject>& JsonObject, TMap<FString, FString>& OutProperties);
};
