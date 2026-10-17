// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"
#include "UMGSchemaTypedPayloads.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

/**
 * Editor-facing JSON parse bridge for preview, import, and debugging workflows.
 */
class DESIGN2UMGEDITOR_API FUMGJsonParser
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
	static bool ParseWidgetNode(const TSharedPtr<class FJsonObject>& JsonObject, FUMGWidgetNode& OutNode, const FString& NodePath, FString* OutError, class FDesignParserBudget& Budget, int32 Depth);
	// Per-payload section parsers: each reads its own section key from the node object.
	static void ParseTextStylePayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGTextStyleData& OutTextStyle);
	static void ParseWidgetStylePayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGWidgetStyleData& OutWidgetStyle);
	static void ParseTextPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGTextData& OutText);
	static void ParseBrushPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGVisualBrushData& OutBrush);
	static void ParseEffectPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGEffectData& OutEffect);
	static void ParseGradientPayload(const TSharedPtr<class FJsonObject>& GradientObject, FUMGVisualBrushData::FGradientData& Gradient);
	static void ParsePathPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGPathData& OutPath);
	static void ParseShadowPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGShadowData& OutShadow);
	static void ParseRenderPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGRenderStyleData& OutRender);
	static void ParseWidgetStatePayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGWidgetStateData& OutWidgetState);
	static void ParseScrollBoxPayload(const TSharedPtr<class FJsonObject>& NodeObject, FUMGScrollBoxData& OutScrollBox);
	static void ParseSlotData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGSlotData& OutSlot);
	static void ParseAnchorsData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGAnchorsData& OutAnchors);
	static void ParseAnchorData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGAnchorData& OutAnchor);
	static void ParseOffsetsData(const TSharedPtr<class FJsonObject>& JsonObject, FUMGOffsetsData& OutOffsets);
};
