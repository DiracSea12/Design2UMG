// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class FJsonObject;

/**
 * Serializes normalized UMG widget data into JSON.
 * It is the counterpart to FUMGJsonParser and is reused by preview and import flows.
 */
class FUMGJsonSerializer
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
	static TSharedRef<FJsonObject> SerializeBounds(const FBounds& Bounds);
};
