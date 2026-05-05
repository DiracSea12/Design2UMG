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
	DESIGN2UMGCORE_API void ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout);
	DESIGN2UMGCORE_API void ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EDesignLayout Layout);
	DESIGN2UMGCORE_API void ApplyPadding(FUMGWidgetNode& Widget, const FDesignPadding& Padding);
	DESIGN2UMGCORE_API void ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EDesignJustifyContent Justify, EDesignSizeMode MainAxisMode = EDesignSizeMode::Fixed, EDesignLayout ParentLayout = EDesignLayout::None);
	DESIGN2UMGCORE_API void ApplyAlignItems(FUMGWidgetNode& Widget, EDesignAlignItems Align, EDesignLayout ParentLayout);
	DESIGN2UMGCORE_API bool HasPadding(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode, EDesignLayout ParentLayout = EDesignLayout::None);
}
