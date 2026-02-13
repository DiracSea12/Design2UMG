// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "PenVariableResolver.h"
#include "UMGSchema.h"

/**
 * .pen 节点树到 UMG Widget 树的转换器（遍历+分发）
 * 布局映射 → FPenLayoutMapper
 * 视觉属性 → FPenVisualMapper
 * 组件展开 → FPenRefExpander
 */
class FPenToUMGConverter
{
public:
	/** 主入口：转换整个文档 */
	static FUMGWidgetData Convert(const FPenDocument& Document);

private:
	/** 递归转换节点 */
	static FUMGWidgetNode ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc, const TMap<FString, FString>& ThemeContext = TMap<FString, FString>(), TArray<FString>* OutWarnings = nullptr);

	/** 创建容器 Widget */
	static FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FPenNode>& Node);

	/** 创建文本 Widget */
	static FUMGWidgetNode CreateTextWidget(const TSharedPtr<FPenNode>& Node);

	/** 创建图标字体 Widget */
	static FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FPenNode>& Node);

	/** 创建 Path Widget（栅格化 SVG geometry 为 Image） */
	static FUMGWidgetNode CreatePathWidget(const TSharedPtr<FPenNode>& Node);

	/** 应用文本属性 */
	static void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node);
};
