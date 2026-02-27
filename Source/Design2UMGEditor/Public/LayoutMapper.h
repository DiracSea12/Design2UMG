// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

/**
 * 布局映射：Slot 属性、Gap、Padding、JustifyContent、AlignItems、SizeBox 包裹
 */
class FLayoutMapper
{
public:
	static void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EPenLayout ParentLayout);
	static void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout);
	static void ApplyPadding(FUMGWidgetNode& Widget, const FPenPadding& Padding);
	static void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify, EPenSizeMode MainAxisMode = EPenSizeMode::Fixed, EPenLayout ParentLayout = EPenLayout::None);
	static void ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout);
	static bool HasPadding(const TSharedPtr<FDesignNode>& Node);
	static FUMGWidgetNode WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode);
};
