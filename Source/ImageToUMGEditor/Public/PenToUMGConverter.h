// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "UMGSchema.h"

/**
 * .pen 节点树到 UMG Widget 树的转换器
 */
class FPenToUMGConverter
{
public:
	/** 主入口：转换整个文档 */
	static FUMGWidgetData Convert(const FPenDocument& Document);

private:
	/** 递归转换节点 */
	static FUMGWidgetNode ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc);

	/** 创建容器 Widget */
	static FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FPenNode>& Node);

	/** 创建文本 Widget */
	static FUMGWidgetNode CreateTextWidget(const TSharedPtr<FPenNode>& Node);

	/** 创建图标字体 Widget */
	static FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FPenNode>& Node);

	/** 应用文本属性 */
	static void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node);

	/** 应用 Slot 属性 */
	static void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node, EPenLayout ParentLayout);

	/** 应用视觉属性 */
	static void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node);

	/** 应用圆角 */
	static void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FPenCornerRadius& Radius);

	/** 应用描边 */
	static void ApplyStroke(FUMGWidgetNode& Widget, const FPenStroke& Stroke);

	/** 展开组件引用 */
	static TSharedPtr<FPenNode> ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc);

	/** 查找可复用组件 */
	static TSharedPtr<FPenNode> FindReusableComponent(const FString& RefId, const TArray<TSharedPtr<FPenNode>>& Nodes);

	/** 应用 gap - 转换为子元素 Padding */
	static void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout);

	/** 应用 padding - 通过 Slot.Padding 实现 */
	static void ApplyPadding(FUMGWidgetNode& Widget, const FPenPadding& Padding);

	/** 应用 justifyContent - 通过 Spacer 实现主轴对齐 */
	static void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify, EPenLayout Layout);

	/** 应用 alignItems - 交叉轴对齐映射到 Slot */
	static void ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout);
};
