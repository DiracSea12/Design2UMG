// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class FJsonObject;

/**
 * JSON 搴忓垪鍖栧櫒 - 灏?UMG 鏁版嵁缁撴瀯搴忓垪鍖栦负 JSON
 *
 * 涓?FUMGJsonParser锛堝弽搴忓垪鍖栵級瀵圭О銆? * 渚涢瑙堢獥鍙ｃ€佸鍏ユ祦绋嬬瓑澶氬澶嶇敤銆? */
class FUMGJsonSerializer
{
public:
	/** 灏?FUMGWidgetData 搴忓垪鍖栦负 Pretty JSON 瀛楃涓?*/
	static FString Serialize(const FUMGWidgetData& Data);

	/** 灏嗗崟涓?FUMGWidgetNode 搴忓垪鍖栦负 FJsonObject */
	static TSharedRef<FJsonObject> SerializeWidgetNode(const FUMGWidgetNode& Node);

private:
	static TSharedRef<FJsonObject> SerializeAnchor(const FUMGAnchorData& Anchor);
	static TSharedRef<FJsonObject> SerializeAnchors(const FUMGAnchorsData& Anchors);
	static TSharedRef<FJsonObject> SerializeOffsets(const FUMGOffsetsData& Offsets);
	static TSharedRef<FJsonObject> SerializeSlot(const FUMGSlotData& Slot);
	static TSharedRef<FJsonObject> SerializeBounds(const FBounds& Bounds);
};

