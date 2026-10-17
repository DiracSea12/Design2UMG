// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "DesignPathRasterDefaults.h"
#include "DesignImageResourceLimits.h"

class UTexture2D;
// Owns all paint descriptions; the optional source texture is borrowed for the synchronous call.
struct FDesignPathRasterRequest
{
	FUMGPathData Path;
	FIntPoint Size = FIntPoint::ZeroValue;
	TOptional<FUMGProceduralResourceRequest> FillPaint;
	TOptional<FUMGProceduralResourceRequest> StrokePaint;
	TOptional<FUMGVisualBrushData::FImageData> FillImage;
	UTexture2D* FillTexture = nullptr;

	FDesignPathRasterRequest() = default;
	FDesignPathRasterRequest(FString Geometry, FIntPoint RasterSize, TOptional<FLinearColor> FillColor)
		: Size(RasterSize)
	{
		Path.Geometry = MoveTemp(Geometry);
		Path.FillColor = FillColor;
	}
};

class FDesignPathRasterizer
{
public:
	// Limits apply to final supersampled pixels, as in the external image decode path.
	static constexpr int32 MaxRasterDimension = DesignImageResources::FImageResourceLimits::DefaultMaxDimension;
	static constexpr int64 MaxRasterPixels = DesignImageResources::FImageResourceLimits::DefaultMaxPixels;
	static bool IsRasterSizeAllowed(int64 Width, int64 Height);
	static bool TryGetRasterSize(float Width, float Height, FIntPoint& OutSize);
	static bool TryBuildRequest(const FUMGWidgetNode& Node, FDesignPathRasterRequest& OutRequest,
		const TMap<FString, UTexture2D*>* TextureMap = nullptr);
	static UTexture2D* Rasterize(const FDesignPathRasterRequest& Request, FString* OutError = nullptr,
		DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static void FlushPendingTextureUpdates();
	static void ClearCache();
	static int64 GetCachedPixelCount();
};
