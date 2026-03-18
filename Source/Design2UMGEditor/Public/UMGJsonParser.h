// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * JSON 瑙ｆ瀽鍣?- 灏?JSON 瀛楃涓茶В鏋愪负 UMG 鏁版嵁缁撴瀯
 */
class FUMGJsonParser
{
public:
	/**
	 * 瑙ｆ瀽 JSON 瀛楃涓蹭负 Widget 鏁版嵁
	 * @param JsonString JSON 瀛楃涓?	 * @param OutWidgetData 杈撳嚭鐨?Widget 鏁版嵁
	 * @param OutError 閿欒淇℃伅锛堝彲閫夛級
	 * @return 瑙ｆ瀽鏄惁鎴愬姛
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

