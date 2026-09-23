// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPathRasterDefaults.h"
#include "DesignImageResourceLimits.h"

class UTexture2D;

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
		// Budget this raster is drawn from, when the caller owns one. Null falls back to
		// the budget published for the current materialization pass (and then to "no
		// budget"), which keeps every pre-existing call site's behaviour unchanged.
		DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static void FlushPendingTextureUpdates();
	static void ClearCache();
	static int64 GetCachedPixelCount();
};
