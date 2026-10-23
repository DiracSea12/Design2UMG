// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "DesignPathRasterDefaults.h"
#include "DesignImageResourceLimits.h"

class UTexture2D;
// Owns paint descriptions; texture pointers are borrowed for the synchronous call.
struct FDesignPathRasterRequest
{
	FUMGPathData Path;
	FIntPoint Size = FIntPoint::ZeroValue;
	// Authored paint coordinates stay fixed while the raster stores only ink.
	FIntPoint PaintSize = FIntPoint::ZeroValue;
	FIntPoint PaintOffset = FIntPoint::ZeroValue;
	// Original drawing quad, including supported stroke outsets, in paint pixels.
	FIntRect CanvasBounds = FIntRect(0, 0, 0, 0);
	TOptional<FUMGProceduralResourceRequest> FillPaint;
	TOptional<FUMGProceduralResourceRequest> StrokePaint;
	TOptional<FUMGVisualBrushData::FImageData> FillImage;
	UTexture2D* FillTexture = nullptr;
	TMap<FString, UTexture2D*> SurfaceTextures;

	bool HasExpandedCanvas() const { return PaintSize.X > 0 && PaintSize.Y > 0 && CanvasBounds != FIntRect(FIntPoint::ZeroValue, PaintSize); }
	FUMGVisualBrushData::FImageTransformData GetCanvasUVTransform() const
	{
		FUMGVisualBrushData::FImageTransformData Transform;
		Transform.A = float(CanvasBounds.Width()) / Size.X;
		Transform.D = float(CanvasBounds.Height()) / Size.Y;
		Transform.TX = float(CanvasBounds.Min.X + PaintOffset.X) / Size.X;
		Transform.TY = float(CanvasBounds.Min.Y + PaintOffset.Y) / Size.Y;
		return Transform;
	}

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
	static void ApplyCanvasTransform(FUMGWidgetNode& Node, const FDesignPathRasterRequest& Request);
	static UTexture2D* Rasterize(const FDesignPathRasterRequest& Request, FString* OutError = nullptr,
		DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static void FlushPendingTextureUpdates();
	static void ClearCache();
	static int64 GetCachedPixelCount();
};
