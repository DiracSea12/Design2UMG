// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * JSON 解析器 - 将 JSON 字符串解析为 UMG 数据结构
 */
class FUMGJsonParser
{
public:
	/**
	 * 解析 JSON 字符串为 Widget 数据
	 * @param JsonString JSON 字符串
	 * @param OutWidgetData 输出的 Widget 数据
	 * @param OutError 错误信息（可选）
	 * @return 解析是否成功
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
