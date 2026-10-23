// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Templates/Function.h"
#include "UMGSchema.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UTexture2D;
struct FSlateBrush;

namespace UMGImageCropUtils
{
	struct FImageCropRequest
	{
		FUMGVisualBrushData::FImageTransformData Transform;
		FString FitMode;
		int32 OutputWidth = 64;
		int32 OutputHeight = 64;
	};

	struct FImageSamplingPlan
	{
		FUMGVisualBrushData::FImageTransformData Transform;
		FVector2D CropScale = FVector2D(1, 1);
		bool bClipToCrop = false;
	};

	bool TryBuildSamplingPlan(const FUMGVisualBrushData::FImageTransformData& Transform, const FString& FitMode,
		const FVector2D& SourceSize, const FVector2D& OutputSize, FImageSamplingPlan& OutPlan);

	bool TryBuildCropRequest(const FUMGWidgetNode& Node, FImageCropRequest& OutRequest);
	bool CanUseUvRegion(const FUMGVisualBrushData::FImageTransformData& Transform);
	bool CanUseUvRegion(const FImageCropRequest& Request);
	bool TryApplyCropToBrush(
		FSlateBrush& InOutBrush,
		UTexture2D* SourceTexture,
		const FImageCropRequest& Request,
		TFunctionRef<void(UTexture2D*)> ApplyAffineCropTexture, DesignImageResources::FImageOperationBudget* Budget = nullptr);
}
