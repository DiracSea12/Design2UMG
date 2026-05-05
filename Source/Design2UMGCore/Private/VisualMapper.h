// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "UMGSchema.h"

struct FDesignNode;

struct FVisualMapperOptions
{
	float LuminosityFitOpacityCap = 0.0f;
};

/**
 * Maps visual properties such as fill, rounded corners, stroke, border,
 * background blur, shadows, and multi-fill wrappers.
 */
namespace VisualMapper
{
	DESIGN2UMGCORE_API void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API void ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width = -1.0f, float Height = -1.0f);
	DESIGN2UMGCORE_API void ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithForegroundBlur(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, float BlurStrength);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FUMGBoxShadowLayerData& ShadowLayer, int32 ShadowIndex);
	DESIGN2UMGCORE_API FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FVisualMapperOptions& Options);
}
