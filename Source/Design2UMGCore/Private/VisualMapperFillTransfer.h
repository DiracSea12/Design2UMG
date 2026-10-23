// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "VisualMapper.h"

struct FDesignNode;
struct FUMGWidgetNode;

namespace VisualMapperFillTransfer
{
    FString CanonicalPaintBlendMode(const FString& Mode);
    FMargin BackgroundCaptureInsets(const FDesignNode& Node);
    void MarkBackdropProducers(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
    bool RequiresBackdropLayers(const FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient);
	FUMGWidgetNode WrapWithPaintedStroke(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, int32 PaintLayerCount = 0);
	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode WrapWithMultiFill(
		FUMGWidgetNode&& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& Options, int32* OutPaintLayerCount = nullptr);
}
