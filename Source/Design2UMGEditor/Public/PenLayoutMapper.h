// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "UMGSchema.h"

/**
 * 布局映射：Slot 属性、Gap、Padding、JustifyContent、AlignItems、SizeBox 包裹
 */
class FPenLayoutMapper
{
public:
	static void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node, EPenLayout ParentLayout);
	static void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout);
	static void ApplyPadding(FUMGWidgetNode& Widget, const FPenPadding& Padding);
	static void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify);
	static void ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout);
	static bool HasPadding(const TSharedPtr<FPenNode>& Node);
	static FUMGWidgetNode WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FPenNode>& ChildNode);
};
