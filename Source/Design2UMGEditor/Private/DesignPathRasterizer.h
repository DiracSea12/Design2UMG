// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaTypedPayloads.h"
#include "DesignPathRasterDefaults.h"
#include "DesignImageResourceLimits.h"

class UTexture2D;
struct FUMGProceduralResourceRequest;

class FDesignPathRasterizer
{
public:
	// Limits apply to final supersampled pixels, as in the external image decode path.
	static constexpr int32 MaxRasterDimension = DesignImageResources::FImageResourceLimits::DefaultMaxDimension;
	static constexpr int64 MaxRasterPixels = DesignImageResources::FImageResourceLimits::DefaultMaxPixels;
	static bool IsRasterSizeAllowed(int64 Width, int64 Height);
	static bool TryGetRasterSize(float Width, float Height, FIntPoint& OutSize);
	static UTexture2D* Rasterize(
		const FString& Geometry,
		int32 Width,
		int32 Height,
		const TOptional<FLinearColor>& FillColor,
		const FString& FillRule = DesignPathRasterDefaults::FillRule,
		const TOptional<FLinearColor>& StrokeColor = TOptional<FLinearColor>(),
		float StrokeWidth = 0.0f,
		const FString& StrokeCap = DesignPathRasterDefaults::StrokeCap,
		const FString& StrokeJoin = DesignPathRasterDefaults::StrokeJoin,
		FString* OutError = nullptr,
		// Share a budget across allocations; null starts a standalone operation.
		DesignImageResources::FImageOperationBudget* Budget = nullptr,
		const FUMGProceduralResourceRequest* FillPaint = nullptr,
		const FString& StrokeGeometry = FString(),
		const FUMGProceduralResourceRequest* StrokePaint = nullptr,
		const FUMGVisualBrushData::FImageData* FillImage = nullptr,
		UTexture2D* FillTexture = nullptr);
	static void FlushPendingTextureUpdates();
	static void ClearCache();
	static int64 GetCachedPixelCount();
};
