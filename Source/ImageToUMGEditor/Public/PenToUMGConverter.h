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

	/** 应用 Slot 属性 */
	static void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node, EPenLayout ParentLayout);

	/** 应用视觉属性 */
	static void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node);

	/** 展开组件引用 */
	static TSharedPtr<FPenNode> ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc);

	/** 查找可复用组件 */
	static TSharedPtr<FPenNode> FindReusableComponent(const FString& RefId, const TArray<TSharedPtr<FPenNode>>& Nodes);
};
