// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"

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
};
