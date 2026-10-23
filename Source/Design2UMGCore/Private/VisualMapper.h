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
	void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width = -1.0f, float Height = -1.0f);
	void ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke);
	FUMGWidgetNode WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, bool* OutStrokePainted = nullptr);
	FUMGWidgetNode WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength);
	FUMGWidgetNode WrapWithShaderEffect(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShaderData& Shader);
	FUMGWidgetNode WrapWithForegroundBlur(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, float BlurStrength);
	FUMGWidgetNode WrapWithGeometryMask(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow);
	FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const TArray<FUMGBoxShadowLayerData>& ShadowLayers, const FDesignShadow* InnerShadow = nullptr);
	FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FVisualMapperOptions& Options);
}
