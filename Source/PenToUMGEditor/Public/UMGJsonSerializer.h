// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class FJsonObject;

/**
 * JSON 序列化器 - 将 UMG 数据结构序列化为 JSON
 *
 * 与 FUMGJsonParser（反序列化）对称。
 * 供预览窗口、导入流程等多处复用。
 */
class FUMGJsonSerializer
{
public:
	/** 将 FUMGWidgetData 序列化为 Pretty JSON 字符串 */
	static FString Serialize(const FUMGWidgetData& Data);

	/** 将单个 FUMGWidgetNode 序列化为 FJsonObject */
	static TSharedRef<FJsonObject> SerializeWidgetNode(const FUMGWidgetNode& Node);

private:
	static TSharedRef<FJsonObject> SerializeAnchor(const FUMGAnchorData& Anchor);
	static TSharedRef<FJsonObject> SerializeAnchors(const FUMGAnchorsData& Anchors);
	static TSharedRef<FJsonObject> SerializeOffsets(const FUMGOffsetsData& Offsets);
	static TSharedRef<FJsonObject> SerializeSlot(const FUMGSlotData& Slot);
	static TSharedRef<FJsonObject> SerializeBounds(const FBounds& Bounds);
};
