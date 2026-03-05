// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

/**
 * 瑙嗚灞炴€ф槧灏勶細Fill銆佸渾瑙掋€佹弿杈广€丅order/BackgroundBlur 鍖呰９
 */
class FVisualMapper
{
public:
	static void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	static void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width = -1.0f, float Height = -1.0f);
	static void ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke);
	static FUMGWidgetNode WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
	static FUMGWidgetNode WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength);
	static FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, int32 ShadowIndex);
	static FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
};


