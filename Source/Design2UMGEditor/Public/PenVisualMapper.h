// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "UMGSchema.h"

/**
 * 视觉属性映射：Fill、圆角、描边、Border/BackgroundBlur 包裹
 */
class FPenVisualMapper
{
public:
	static void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node);
	static void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FPenCornerRadius& Radius);
	static void ApplyStroke(FUMGWidgetNode& Widget, const FPenStroke& Stroke);
	static FUMGWidgetNode WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node);
	static FUMGWidgetNode WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength);
	static FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node);
	static FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node);
};
