// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGImageCropUtils.h"

#include "Design2UMGWidgetPropertyAccess.h"
#include "ProceduralShapeGenerator.h"
#include "DesignImageResourceLimits.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Styling/SlateBrush.h"

namespace
{
	bool IsCropMode(const FString& Mode)
	{
		return Mode.Equals(TEXT("Crop"), ESearchCase::CaseSensitive);
	}

	bool TryGetSourcePixelSize(const UTexture2D* Texture, FIntPoint& OutSize)
	{
		if (!Texture) return false;
#if WITH_EDITORONLY_DATA
		if (Texture->Source.IsValid())
		{
			OutSize = FIntPoint(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
			return DesignImageResources::FImageResourceLimits().AllowsDimensions(OutSize.X, OutSize.Y);
		}
#endif
		const FTexturePlatformData* Data = Texture->GetPlatformData();
		if (!Data || Data->Mips.IsEmpty() || Texture->GetPixelFormat() != PF_B8G8R8A8) return false;
		OutSize = FIntPoint(Data->Mips[0].SizeX, Data->Mips[0].SizeY);
		return DesignImageResources::FImageResourceLimits().AllowsDimensions(OutSize.X, OutSize.Y);
	}
}

bool UMGImageCropUtils::TryBuildSamplingPlan(
	const FUMGVisualBrushData::FImageTransformData& Transform, const FString& FitMode,
	const FVector2D& SourceSize, const FVector2D& OutputSize, FImageSamplingPlan& OutPlan)
{
	FImageSamplingPlan Plan;
	Plan.Transform = Transform;
	if (SourceSize.X <= 0 || SourceSize.Y <= 0 || OutputSize.X <= 0 || OutputSize.Y <= 0)
	{
		return false;
	}
	if (!FitMode.IsEmpty())
	{
		const FVector2D CropWidth(Transform.A * SourceSize.X, Transform.C * SourceSize.Y);
		const FVector2D CropHeight(Transform.B * SourceSize.X, Transform.D * SourceSize.Y);
		const FVector2D CropSize(CropWidth.Size(), CropHeight.Size());
		if (CropSize.X <= 0 || CropSize.Y <= 0)
		{
			return false;
		}
		if (FitMode == TEXT("Fill") || FitMode == TEXT("Fit"))
		{
			const FVector2D Scale = OutputSize / CropSize;
			const double UniformScale = FitMode == TEXT("Fill") ? FMath::Max(Scale.X, Scale.Y) : FMath::Min(Scale.X, Scale.Y);
			Plan.CropScale = Scale / UniformScale;
			Plan.bClipToCrop = FitMode == TEXT("Fit");
			if (!Plan.bClipToCrop)
			{
				// Cover must stay inside the source image, including a moved or rotated crop.
				FVector2D HalfExtent(
					0.5 * (FMath::Abs(Transform.A) * Plan.CropScale.X + FMath::Abs(Transform.B) * Plan.CropScale.Y),
					0.5 * (FMath::Abs(Transform.C) * Plan.CropScale.X + FMath::Abs(Transform.D) * Plan.CropScale.Y));
				const double Shrink = FMath::Min(1.0, 0.5 / FMath::Max(HalfExtent.X, HalfExtent.Y));
				Plan.CropScale *= Shrink;
				HalfExtent *= Shrink;
				const FVector2D Center(0.5 * (Transform.A + Transform.B) + Transform.TX,
					0.5 * (Transform.C + Transform.D) + Transform.TY);
				Plan.Transform.TX += static_cast<float>(FMath::Clamp(Center.X, HalfExtent.X, 1.0 - HalfExtent.X) - Center.X);
				Plan.Transform.TY += static_cast<float>(FMath::Clamp(Center.Y, HalfExtent.Y, 1.0 - HalfExtent.Y) - Center.Y);
			}
		}
		else if (FitMode != TEXT("Stretch"))
		{
			return false;
		}
	}
	const FIntPoint OriginalSize(FMath::RoundToInt(SourceSize.X), FMath::RoundToInt(SourceSize.Y));
	Plan.PrefilterSize = OriginalSize;
	if (CanUseUvRegion(Plan.Transform))
	{
		const double FootprintX = FMath::Abs(Plan.Transform.A * Plan.CropScale.X) * SourceSize.X / OutputSize.X;
		const double FootprintY = FMath::Abs(Plan.Transform.D * Plan.CropScale.Y) * SourceSize.Y / OutputSize.Y;
		Plan.PrefilterSize.X = FMath::Clamp(FMath::CeilToInt(SourceSize.X / FMath::Max(1.0, FootprintX)), 1, OriginalSize.X);
		Plan.PrefilterSize.Y = FMath::Clamp(FMath::CeilToInt(SourceSize.Y / FMath::Max(1.0, FootprintY)), 1, OriginalSize.Y);
		Plan.bPrefilter = Plan.PrefilterSize != OriginalSize;
	}
	OutPlan = Plan;
	return true;
}

bool UMGImageCropUtils::TryBuildCropRequest(const FUMGWidgetNode& Node, FImageCropRequest& OutRequest)
{
	const FString Mode = Node.Brush.Image.Mode.Get(FString());
	if (!IsCropMode(Mode))
	{
		return false;
	}

	if (!Node.Brush.Image.Transform.IsSet()) return false;
	OutRequest.Transform = Node.Brush.Image.Transform.GetValue();
	OutRequest.FitMode = Node.Brush.Image.CropFitMode.Get(FString());

	const float CropWidth = Node.WidgetStyle.WidthOverride.Get(OutRequest.FitMode.IsEmpty() ? 64.0f : Node.Bounds.W);
	const float CropHeight = Node.WidgetStyle.HeightOverride.Get(OutRequest.FitMode.IsEmpty() ? 64.0f : Node.Bounds.H);
	FIntPoint OutputSize;
	// An invalid pixel size remains zero and is rejected before crop allocation.
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

bool UMGImageCropUtils::CanUseUvRegion(const FImageCropRequest& Request)
{
	return Request.FitMode.IsEmpty() && CanUseUvRegion(Request.Transform);
}

bool UMGImageCropUtils::CanUseUvShortcut(const FImageCropRequest& Request, const UTexture2D* Texture)
{
	FIntPoint SourceSize;
	FImageSamplingPlan Sampling;
	return CanUseUvRegion(Request) && TryGetSourcePixelSize(Texture, SourceSize)
		&& TryBuildSamplingPlan(Request.Transform, Request.FitMode, FVector2D(SourceSize),
			FVector2D(Request.OutputWidth, Request.OutputHeight), Sampling) && !Sampling.bPrefilter;
}

bool UMGImageCropUtils::TryApplyCropToBrush(
	FSlateBrush& InOutBrush,
	UTexture2D* SourceTexture,
	const FImageCropRequest& Request,
	TFunctionRef<void(UTexture2D*)> ApplyAffineCropTexture, DesignImageResources::FImageOperationBudget* Budget)
{
	const FUMGVisualBrushData::FImageTransformData& Transform = Request.Transform;
	if (CanUseUvShortcut(Request, SourceTexture))
	{
		Design2UMGWidgetPropertyAccess::SetUVRegion(InOutBrush, FBox2f(
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
		Request.OutputHeight, nullptr, Budget, Request.FitMode, nullptr,
		CanUseUvRegion(Request) ? FProceduralShapeGenerator::ECropAddressMode::Clamp : FProceduralShapeGenerator::ECropAddressMode::Transparent))
	{
		ApplyAffineCropTexture(Cropped);
		Design2UMGWidgetPropertyAccess::SetUVRegion(InOutBrush, FBox2f(FVector2f::ZeroVector, FVector2f(1, 1)));
		InOutBrush.Tiling = ESlateBrushTileType::NoTile;
		InOutBrush.ImageSize = FVector2D(Request.OutputWidth, Request.OutputHeight);
		return true;
	}

	return false;
}
