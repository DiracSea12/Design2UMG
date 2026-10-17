// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "WidgetResourceBinder.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "DesignImageResourceLimits.h"
#include "DesignPathRasterizer.h"
#include "DesignPathRasterDefaults.h"
#include "DesignResourceResolver.h"
#include "Design2UMGLog.h"
#include "UMGImageCropUtils.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetNodeAccess.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"

namespace
{
	UTexture2D* ResolvePathTexture(const FUMGWidgetNode& Node, FIntPoint& Size, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap)
	{
		if (!Node.Path.Geometry.IsSet()) return nullptr;
		if (!FDesignPathRasterizer::TryGetRasterSize(
			Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)),
			Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)), Size)) return nullptr;
		FUMGProceduralResourceRequest Paint;
		const bool bHasGradient = !Node.Brush.Gradient.Stops.IsEmpty()
			&& Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Paint);
		FUMGProceduralResourceRequest StrokePaint;
		const bool bHasStrokeGradient = Node.Path.StrokeGradient.HasAny();
		if (bHasStrokeGradient)
		{
			StrokePaint.Gradient = Node.Path.StrokeGradient;
			StrokePaint.MaterialRef = Design2UMGWidgetNodeAccess::GetGradientMaterialRef(StrokePaint.Gradient);
			StrokePaint.WidthOverride = Node.WidgetStyle.WidthOverride;
			StrokePaint.HeightOverride = Node.WidgetStyle.HeightOverride;
		}
		TOptional<FLinearColor> Fill = Node.Path.FillColor;
		if (bHasGradient && Fill.IsSet() && !bHasStrokeGradient) Fill = FLinearColor::White;
		const bool bHasImage = !bHasGradient && !Node.Brush.Image.TexturePath.Get(FString()).IsEmpty();
		UTexture2D* FillTexture = bHasImage && TextureMap
			? TextureMap->FindRef(Node.Brush.Image.TexturePath.GetValue()) : nullptr;
		UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
			Node.Path.Geometry.GetValue(), Size.X, Size.Y, Fill,
			Node.Path.FillRule.Get(DesignPathRasterDefaults::FillRule), Node.Path.StrokeColor,
			Node.Path.StrokeThickness.Get(0.0f), Node.Path.StrokeCap.Get(DesignPathRasterDefaults::StrokeCap),
			Node.Path.StrokeJoin.Get(DesignPathRasterDefaults::StrokeJoin), nullptr, Budget,
			bHasGradient ? &Paint : nullptr, Node.Path.StrokeGeometry.Get(FString()),
			bHasStrokeGradient ? &StrokePaint : nullptr, bHasImage ? &Node.Brush.Image : nullptr, FillTexture);
		return Texture;
	}

	bool BindPathTexture(UImage* Image, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap)
	{
		FIntPoint Size;
		UTexture2D* Texture = ResolvePathTexture(Node, Size, Budget, TextureMap);
		if (!Texture) return false;
		Image->SetBrushFromTexture(Texture);
		FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*Image);
		Brush.ImageSize = FVector2D(Size.X / DesignPathRasterDefaults::Supersample, Size.Y / DesignPathRasterDefaults::Supersample);
		Brush.Tiling = ESlateBrushTileType::NoTile;
		Image->SetBrush(Brush);
		Image->SetDesiredSizeOverride(Brush.ImageSize);

		return true;
	}
}

FWidgetResourceBinder::EProceduralTextureResult FWidgetResourceBinder::TryResolveProceduralTexture(
	const FUMGWidgetNode& Node,
	UObject* Outer,
	UTexture2D*& OutTexture,
	FUMGProceduralResourceRequest* OutRequest, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap)
{
	if (Node.Path.Geometry.IsSet())
	{
		FIntPoint Size;
		OutTexture = ResolvePathTexture(Node, Size, Budget, TextureMap);
		return OutTexture ? EProceduralTextureResult::Resolved : EProceduralTextureResult::UnknownShape;
	}

	FUMGProceduralResourceRequest ResourceRequest;
	if (!Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, ResourceRequest))
	{
		return EProceduralTextureResult::NoRequest;
	}

	if (OutRequest)
	{
		*OutRequest = ResourceRequest;
	}

	if (ResourceRequest.MaterialRef.IsEmpty())
	{
		return EProceduralTextureResult::EmptyMaterialRef;
	}

	OutTexture = DesignResourceResolver::ResolveTextureFromMaterialRef(ResourceRequest, Outer, Budget);
	return OutTexture != nullptr ? EProceduralTextureResult::Resolved : EProceduralTextureResult::UnknownShape;
}

bool FWidgetResourceBinder::BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	if (Node.Path.Geometry.IsSet()) return BindPathTexture(ImageWidget, Node, Budget, TextureMap);
	// --- material (procedural shape) path ---
	UTexture2D* ProceduralTexture = nullptr;
	FUMGProceduralResourceRequest ResourceRequest;
	switch (TryResolveProceduralTexture(Node, ImageWidget, ProceduralTexture, &ResourceRequest, Budget))
	{
	case EProceduralTextureResult::Resolved:
	{
		UTexture2D* Texture = ProceduralTexture;
		ImageWidget->SetBrushFromTexture(Texture);
		FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget);
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ImageWidget->SetBrush(Brush);
		ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		ImageWidget->SynchronizeProperties();
		return true;
	}
	case EProceduralTextureResult::EmptyMaterialRef:
		return BindPathTexture(ImageWidget, Node, Budget, TextureMap);
	case EProceduralTextureResult::UnknownShape:
		UE_LOG(LogDesign2UMG, Warning, TEXT("Image '%s' has unknown procedural shape type '%s'; falling back to TexturePath."), *Node.Name, *ResourceRequest.MaterialRef);
		break;
	case EProceduralTextureResult::NoRequest:
	default:
		break;
	}

	// --- texture fallback path ---
	const FString TexPath = Node.Brush.Image.TexturePath.Get(FString());
	const FString TrimmedPath = TexPath.TrimStartAndEnd();
	if (TrimmedPath.IsEmpty())
	{
		return BindPathTexture(ImageWidget, Node, Budget, TextureMap);
	}

	UTexture2D* Texture = nullptr;
	if (TextureMap)
	{
		if (UTexture2D* const* Found = TextureMap->Find(TrimmedPath))
		{
			Texture = *Found;
		}
	}
	if (!Texture)
	{
		Texture = DesignResourceResolver::LoadTextureFromPath(TrimmedPath, DesignResourceResolver::ELoadMode::LoadOnly);
	}
	if (!Texture)
	{
		return BindPathTexture(ImageWidget, Node, Budget, TextureMap);
	}

	const FSlateBrush OldBrush = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget);
	const FVector2D OldImageSize = OldBrush.ImageSize;
	const FBox2f OldUVRegion = Design2UMGWidgetPropertyAccess::GetUVRegion(OldBrush);

	ImageWidget->SetBrushFromTexture(Texture);
	FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget);
	Brush.Tiling = ESlateBrushTileType::NoTile;

	const bool bHadSizeOverride = Node.WidgetStyle.WidthOverride.IsSet() || Node.WidgetStyle.HeightOverride.IsSet();
	Brush.ImageSize = bHadSizeOverride ? OldImageSize : FVector2D(Texture->GetSizeX(), Texture->GetSizeY());

	if (OldUVRegion.GetSize().GetMax() > KINDA_SMALL_NUMBER)
	{
		Design2UMGWidgetPropertyAccess::SetUVRegion(Brush, OldUVRegion);
	}

	UMGImageCropUtils::FImageCropRequest CropRequest;
	if (UMGImageCropUtils::TryBuildCropRequest(Node, CropRequest))
	{
		UMGImageCropUtils::TryApplyCropToBrush(
			Brush,
			Texture,
			CropRequest,
			[ImageWidget, &Brush](UTexture2D* Cropped)
			{
				ImageWidget->SetBrushFromTexture(Cropped);
				Brush = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget);
			}, Budget);
	}

	ImageWidget->SetBrush(Brush);
	ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
	ImageWidget->SynchronizeProperties();
	return true;
}
