// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGImageCropUtils.h"
#include "Design2UMGLog.h"

#include "ProceduralShapeGenerator.h"
#include "DesignImageResourceLimits.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

namespace
{
	bool IsCropMode(const FString& Mode)
	{
		return Mode.Equals(TEXT("Crop"), ESearchCase::CaseSensitive);
	}
}

bool UMGImageCropUtils::TryBuildCropRequest(const FUMGWidgetNode& Node, FImageCropRequest& OutRequest)
{
	const FString Mode = Node.WidgetStyle.ImageMode.Get(Node.Brush.Image.Mode.Get(FString()));
	if (!IsCropMode(Mode))
	{
		return false;
	}

	if (Node.Brush.Image.Transform.IsSet())
	{
		OutRequest.Transform = Node.Brush.Image.Transform.GetValue();
	}
	else
	{
		if (!Node.WidgetStyle.ImageTransform.IsSet())
		{
			return false;
		}
		if (!Design2UMGWidgetNodeAccess::TryParseImageTransform(Node.WidgetStyle.ImageTransform.GetValue(), OutRequest.Transform))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("UMGImageCropUtils: Invalid image transform '%s'; skipping crop."), *Node.WidgetStyle.ImageTransform.GetValue());
			return false;
		}
	}

	const float CropWidth = Node.WidgetStyle.WidthOverride.Get(64.0f);
	const float CropHeight = Node.WidgetStyle.HeightOverride.Get(64.0f);
	FIntPoint OutputSize;
	// An invalid pixel size remains zero. UV-only cropping needs no raster; the
	// affine materialization path reports this invalid size before allocating.
	DesignImageResources::FImageResourceLimits().TryGetPixelSize(CropWidth, CropHeight, OutputSize);
	OutRequest.OutputWidth = OutputSize.X;
	OutRequest.OutputHeight = OutputSize.Y;
	return true;
}

bool UMGImageCropUtils::CanUseUvRegion(const FUMGVisualBrushData::FImageTransformData& Transform)
{
	return FMath::IsNearlyZero(Transform.B)
		&& FMath::IsNearlyZero(Transform.C)
		&& Transform.A > KINDA_SMALL_NUMBER
		&& Transform.D > KINDA_SMALL_NUMBER;
}

bool UMGImageCropUtils::TryApplyCropToBrush(
	FSlateBrush& InOutBrush,
	UTexture2D* SourceTexture,
	const FImageCropRequest& Request,
	TFunctionRef<void(UTexture2D*)> ApplyAffineCropTexture)
{
	const FUMGVisualBrushData::FImageTransformData& Transform = Request.Transform;
	if (CanUseUvRegion(Transform))
	{
		InOutBrush.SetUVRegion(FBox2f(
			FVector2f(Transform.TX, Transform.TY),
			FVector2f(Transform.TX + Transform.A, Transform.TY + Transform.D)));
		InOutBrush.Tiling = ESlateBrushTileType::NoTile;
		return true;
	}

	if (!SourceTexture)
	{
		return false;
	}

	if (UTexture2D* Cropped = FProceduralShapeGenerator::ApplyAffineCrop(
		SourceTexture,
		Transform.A,
		Transform.B,
		Transform.TX,
		Transform.C,
		Transform.D,
		Transform.TY,
		Request.OutputWidth,
		Request.OutputHeight))
	{
		ApplyAffineCropTexture(Cropped);
		InOutBrush.Tiling = ESlateBrushTileType::NoTile;
		InOutBrush.ImageSize = FVector2D(Request.OutputWidth, Request.OutputHeight);
		return true;
	}

	return false;
}

