// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGImageCropUtils.h"

#include "ProceduralShapeGenerator.h"
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

bool UMGImageCropUtils::TryBuildCropRequest(const FUMGPropertyBag& Properties, FImageCropRequest& OutRequest)
{
	FString Mode;
	if (!Properties.TryGetString(UMGPropertyKeys::ImageMode, Mode) || !IsCropMode(Mode))
	{
		return false;
	}

	FString TransformString;
	if (!Properties.TryGetString(UMGPropertyKeys::ImageTransform, TransformString)
		|| !Design2UMGWidgetNodeAccess::TryParseImageTransform(TransformString, OutRequest.Transform))
	{
		return false;
	}

	float CropWidth = 64.0f;
	float CropHeight = 64.0f;
	Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, CropWidth);
	Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, CropHeight);
	OutRequest.OutputWidth = FMath::Max(1, FMath::RoundToInt(CropWidth));
	OutRequest.OutputHeight = FMath::Max(1, FMath::RoundToInt(CropHeight));
	return true;
}

bool UMGImageCropUtils::TryBuildCropRequest(const FUMGWidgetNode& Node, FImageCropRequest& OutRequest)
{
	FString Mode;
	if (!Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::ImageMode, Mode) || !IsCropMode(Mode))
	{
		return false;
	}

	FString TransformString;
	if (!Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::ImageTransform, TransformString)
		|| !Design2UMGWidgetNodeAccess::TryParseImageTransform(TransformString, OutRequest.Transform))
	{
		return false;
	}

	float CropWidth = 64.0f;
	float CropHeight = 64.0f;
	Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::WidthOverride, CropWidth);
	Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::HeightOverride, CropHeight);
	OutRequest.OutputWidth = FMath::Max(1, FMath::RoundToInt(CropWidth));
	OutRequest.OutputHeight = FMath::Max(1, FMath::RoundToInt(CropHeight));
	return true;
}

bool UMGImageCropUtils::TryApplyCropToBrush(
	FSlateBrush& InOutBrush,
	UTexture2D* SourceTexture,
	const FImageCropRequest& Request,
	TFunctionRef<void(UTexture2D*)> ApplyAffineCropTexture)
{
	const FUMGVisualBrushData::FImageTransformData& Transform = Request.Transform;
	if (FMath::IsNearlyZero(Transform.B) && FMath::IsNearlyZero(Transform.C)
		&& Transform.A > KINDA_SMALL_NUMBER && Transform.D > KINDA_SMALL_NUMBER)
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

