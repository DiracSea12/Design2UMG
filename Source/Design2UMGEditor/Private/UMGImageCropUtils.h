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
		int32 OutputWidth = 64;
		int32 OutputHeight = 64;
	};

	bool TryBuildCropRequest(const FUMGWidgetNode& Node, FImageCropRequest& OutRequest);
	bool CanUseUvRegion(const FUMGVisualBrushData::FImageTransformData& Transform);
	bool TryApplyCropToBrush(
		FSlateBrush& InOutBrush,
		UTexture2D* SourceTexture,
		const FImageCropRequest& Request,
		TFunctionRef<void(UTexture2D*)> ApplyAffineCropTexture, DesignImageResources::FImageOperationBudget* Budget = nullptr);
}
