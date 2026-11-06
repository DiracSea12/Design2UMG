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
	/**
	 * Rounded Figma surface drawn through its exact source contour. Animated surfaces keep it so both endpoints share
	 * one silhouette; static ones need it only for smoothed corners or clipped content that crosses a corner arc.
	 */
	bool RequiresSourceContour(const FDesignNode& Node, bool bAnimatedSurface);
	bool RequiresSourceContour(const FDesignNode& Node);
	/** Child paint box, including its outer shadows and render transform, in the parent's layout space. */
	FBox2D ChildPaintBounds(const FDesignNode& Child);
	bool BuildSourceContour(const FDesignNode& Node, FUMGEffectData& Contour, FString& Error);
	FUMGWidgetNode WrapWithSourceContour(FUMGWidgetNode&& Widget, const FUMGEffectData& Contour);
	void ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width = -1.0f, float Height = -1.0f);
	void ApplyStrokeLayout(FUMGWidgetNode& Widget, const FDesignStroke& Stroke);
	FUMGWidgetNode WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, bool* OutStrokePainted = nullptr);
	FUMGWidgetNode WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength);
	FUMGWidgetNode WrapWithShaderEffect(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShaderData& Shader);
	FUMGWidgetNode WrapWithForegroundBlur(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, float BlurStrength);
	FUMGWidgetNode WrapWithGeometryMask(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow);
	FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const TArray<FUMGBoxShadowLayerData>& ShadowLayers, const FDesignShadow* InnerShadow = nullptr);
	FUMGWidgetNode WrapWithMultiFill(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FVisualMapperOptions& Options);
}
