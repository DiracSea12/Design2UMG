// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"
#include "UMGSchemaTypedPayloads.h"

class FJsonObject;
struct FUMGWidgetData;
struct FUMGWidgetNode;

/**
 * Editor-facing JSON serialize bridge/debug utility for preview and import workflows.
 * Kept public for source compatibility; this is not a runtime module API promise.
 */
class DESIGN2UMGEDITOR_API FUMGJsonSerializer
{
public:
	/** Serialize widget data into pretty-printed JSON. */
	static FString Serialize(const FUMGWidgetData& Data);

	/** Serialize a single widget node into a JSON object. */
	static TSharedRef<FJsonObject> SerializeWidgetNode(const FUMGWidgetNode& Node);

private:
	static TSharedRef<FJsonObject> SerializeAnchor(const FUMGAnchorData& Anchor);
	static TSharedRef<FJsonObject> SerializeAnchors(const FUMGAnchorsData& Anchors);
	static TSharedRef<FJsonObject> SerializeOffsets(const FUMGOffsetsData& Offsets);
	static TSharedRef<FJsonObject> SerializeSlot(const FUMGSlotData& Slot);
	static TSharedRef<FJsonObject> SerializeBounds(const FDesignPixelBounds& Bounds);
	// Per-payload section serializers: a null return means the section is omitted entirely.
	static TSharedPtr<FJsonObject> SerializeTextStylePayload(const FUMGTextStyleData& TextStyle);
	static TSharedPtr<FJsonObject> SerializeWidgetStylePayload(const FUMGWidgetStyleData& WidgetStyle);
	static TSharedPtr<FJsonObject> SerializeTextPayload(const FUMGTextData& Text);
	static TSharedPtr<FJsonObject> SerializeBrushPayload(const FUMGVisualBrushData& Brush);
	static TSharedPtr<FJsonObject> SerializeEffectPayload(const FUMGEffectData& Effect);
	static TSharedPtr<FJsonObject> SerializePathPayload(const FUMGPathData& Path);
	static TSharedPtr<FJsonObject> SerializeShadowPayload(const FUMGShadowData& Shadow);
	static TSharedPtr<FJsonObject> SerializeRenderPayload(const FUMGRenderStyleData& Render);
	static TSharedPtr<FJsonObject> SerializeWidgetStatePayload(const FUMGWidgetStateData& WidgetState);
	static TSharedPtr<FJsonObject> SerializeScrollBoxPayload(const FUMGScrollBoxData& ScrollBox);
};
