// Copyright Epic Games, Inc. All Rights Reserved.

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
};
