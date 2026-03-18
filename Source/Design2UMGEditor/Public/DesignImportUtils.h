// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

struct FUMGWidgetData;
class UWidgetBlueprint;

/**
 * Design2UMG 瀵煎叆娴佺▼鍏变韩宸ュ叿鍑芥暟
 *
 * 鎻愪緵 Design2UMGEditorModule 鍜?PenFileImportFactory 鍏辩敤鐨勶細
 * - 棰勮绐楀彛鎵撳紑
 * - JSON 缂栬緫鍚庣敓鎴? * - Frame 閫夋嫨鍣?UI
 * - Widget Blueprint 鐢熸垚
 */
namespace DesignImportUtils
{
	/** 鐢熸垚 Widget Blueprint 骞惰繑鍥?*/
	UWidgetBlueprint* GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 鐢熸垚 Widget Blueprint + 娓叉煋 PNG 棰勮鍥?*/
	bool GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 生成并编译 Blueprint，然后渲染最终可打开状态的 PNG。*/
	bool GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FString* OutGeneratedAssetPath = nullptr,
		FIntPoint* OutRenderedSize = nullptr);

	/** 对现有 Widget Blueprint 做编译后渲染并保存为 PNG。*/
	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		const FIntPoint& RequestedRenderSize = FIntPoint::ZeroValue,
		FIntPoint* OutRenderedSize = nullptr);

	/** 浠庣紪杈戝悗鐨?JSON 瑙ｆ瀽 + 杩滅▼鍥剧墖 + 鐢熸垚 Blueprint */
	void GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath);

	/** 瑙ｆ瀽杩滅▼鍥剧墖寮曠敤骞跺鍏?*/
	void ResolveImages(FUMGWidgetData& Data);

	/**
	 * 鎵撳紑棰勮绐楀彛
	 * @return true 琛ㄧず绐楀彛宸叉墦寮€
	 */
	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FText& WindowTitle);

	/**
	 * 寮瑰嚭 Frame 閫夋嫨鍣ㄥ璇濇
	 * @param OnFrameSelected 鐢ㄦ埛鍙屽嚮閫夋嫨鍚庣殑鍥炶皟锛堟帴鏀?FrameId锛?	 */
	using FFrameSelectedCallback = TFunction<void(const FString& FrameId)>;
	void ShowFrameSelector(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected);
}



