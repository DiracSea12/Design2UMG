// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "UMGSchema.h"

struct FDesignNode;

/**
 * Layout mapping helpers for slot data, gap, padding, alignment, and SizeBox wrapping.
 */
namespace LayoutMapper
{
	void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout);
	void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EDesignLayout Layout);
	void ApplyPadding(FUMGWidgetNode& Widget, const FDesignPadding& Padding);
	void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EDesignJustifyContent Justify, EDesignSizeMode MainAxisMode = EDesignSizeMode::Fixed, EDesignLayout ParentLayout = EDesignLayout::None);
	void ApplyAlignItems(FUMGWidgetNode& Widget, EDesignAlignItems Align, EDesignLayout ParentLayout);
	bool HasPadding(const TSharedPtr<FDesignNode>& Node);
	/**
	 * Fuses the SizeBox constraints into a widget class that has an equivalent
	 * editor-side Slate implementation. Unsupported widget types stay wrapped.
	 */
	bool TryFuseSizeConstraints(FUMGWidgetNode& ChildWidget, const FUMGWidgetNode& SizeBoxWidget);
	FUMGWidgetNode WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode, EDesignLayout ParentLayout = EDesignLayout::None);
}
