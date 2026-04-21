// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "UMGSchema.h"

struct FDesignNode;

/**
 * Layout mapping helpers for slot data, gap, padding, alignment, and SizeBox wrapping.
 */
class DESIGN2UMGCORE_API FLayoutMapper
{
public:
	static void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout);
	static void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EDesignLayout Layout);
	static void ApplyPadding(FUMGWidgetNode& Widget, const FDesignPadding& Padding);
	static void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EDesignJustifyContent Justify, EDesignSizeMode MainAxisMode = EDesignSizeMode::Fixed, EDesignLayout ParentLayout = EDesignLayout::None);
	static void ApplyAlignItems(FUMGWidgetNode& Widget, EDesignAlignItems Align, EDesignLayout ParentLayout);
	static bool HasPadding(const TSharedPtr<FDesignNode>& Node);
	static FUMGWidgetNode WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode, EDesignLayout ParentLayout = EDesignLayout::None);
};


