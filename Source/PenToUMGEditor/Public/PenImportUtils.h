// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;
struct FPenFrameInfo;
class UWidgetBlueprint;

/**
 * PenToUMG 导入流程共享工具函数
 *
 * 提供 PenToUMGEditorModule 和 PenFileImportFactory 共用的：
 * - 预览窗口打开
 * - JSON 编辑后生成
 * - Frame 选择器 UI
 * - Widget Blueprint 生成
 */
namespace PenImportUtils
{
	/** 生成 Widget Blueprint 并返回 */
	UWidgetBlueprint* GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 生成 Widget Blueprint + 渲染 PNG 预览图 */
	bool GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 从编辑后的 JSON 解析 + 远程图片 + 生成 Blueprint */
	void GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath);

	/** 解析远程图片引用并导入 */
	void ResolveImages(FUMGWidgetData& Data);

	/**
	 * 打开预览窗口
	 * @param OnGenerate 用户确认后的生成回调（接收 EditedJson 和 OutputPath）
	 * @return true 表示窗口已打开
	 */
	using FGenerateCallback = TFunction<void(const FString& EditedJson, const FString& OutputPath)>;
	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FText& WindowTitle);

	/**
	 * 弹出 Frame 选择器对话框
	 * @param OnFrameSelected 用户双击选择后的回调（接收 FrameId）
	 */
	using FFrameSelectedCallback = TFunction<void(const FString& FrameId)>;
	void ShowFrameSelector(
		const TArray<FPenFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected);
}
