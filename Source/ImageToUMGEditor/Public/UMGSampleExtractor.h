// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"
#include "UMGSchema.h"

/**
 * 从UMG提取的测试样例
 */
struct FUMGExtractedSample
{
	FString WidgetName;
	FString WidgetType;
	FBounds Bounds;           // 从Slot反推的Bounds
	FUMGSlotData OriginalSlot; // 原始Slot数据
};

/**
 * UMG样例提取器 - 从真实Widget Blueprint提取Slot数据
 */
class FUMGSampleExtractor
{
public:
	/**
	 * 从Widget Blueprint提取所有CanvasPanelSlot数据
	 * @param WidgetBlueprintPath Widget Blueprint资产路径
	 * @param OutSamples 输出的样例数组
	 * @return 提取的样例数量
	 */
	static int32 ExtractFromWidgetBlueprint(
		const FString& WidgetBlueprintPath,
		TArray<FUMGExtractedSample>& OutSamples);

	/**
	 * 从Slot数据反推Bounds
	 * @param Slot 原始Slot数据
	 * @param DesignSize 设计尺寸
	 * @return 反推的Bounds
	 */
	static FBounds SlotToBounds(
		const FUMGSlotData& Slot,
		const FIntPoint& DesignSize);

	/**
	 * 导出样例到JSON文件
	 */
	static bool ExportToJson(
		const TArray<FUMGExtractedSample>& Samples,
		const FString& OutputPath);

	/**
	 * 从JSON文件加载样例
	 */
	static bool LoadFromJson(
		const FString& JsonPath,
		TArray<FUMGExtractedSample>& OutSamples);
};
