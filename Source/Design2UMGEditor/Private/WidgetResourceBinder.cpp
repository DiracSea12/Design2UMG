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
#include "Materials/MaterialInterface.h"

namespace
{
	bool BindPathTexture(UImage* Image, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap)
	{
		FDesignPathRasterRequest Request;
		if (!FDesignPathRasterizer::TryBuildRequest(Node, Request, TextureMap)) return false;
		UTexture2D* Texture = FDesignPathRasterizer::Rasterize(Request, nullptr, Budget);
		if (!Texture) return false;
		Image->SetBrushFromTexture(Texture);
		FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*Image);
		Brush.ImageSize = FVector2D(Request.PaintSize.X / DesignPathRasterDefaults::Supersample, Request.PaintSize.Y / DesignPathRasterDefaults::Supersample);
		if (Node.Path.CanvasSize.IsSet() || Node.Path.StrokeAlignment.IsSet())
			Brush.ImageSize = FVector2D(Node.WidgetStyle.WidthOverride.Get(Brush.ImageSize.X), Node.WidgetStyle.HeightOverride.Get(Brush.ImageSize.Y));
		Brush.Tiling = ESlateBrushTileType::NoTile;
		const auto UV = Request.GetCanvasUVTransform();
		Design2UMGWidgetPropertyAccess::SetUVRegion(Brush, FBox2f(FVector2f(UV.TX, UV.TY), FVector2f(UV.TX + UV.A, UV.TY + UV.D)));
		Image->SetBrush(Brush);
		Image->SetDesiredSizeOverride(Brush.ImageSize);
		FUMGWidgetNode Expanded = Node;
		FDesignPathRasterizer::ApplyCanvasTransform(Expanded, Request);
		if (Request.HasExpandedCanvas())
		{
			Image->SetRenderScale(Expanded.Render.RenderTransformScale.Get(FVector2D(1, 1)));
			Image->SetRenderTranslation(Expanded.Render.RenderTranslation.Get(FVector2D::ZeroVector));
		}

		return true;
	}
}

FWidgetResourceBinder::EProceduralTextureResult FWidgetResourceBinder::TryResolveProceduralTexture(
	const FUMGWidgetNode& Node,
	UObject* Outer,
	UTexture2D*& OutTexture,
	FUMGProceduralResourceRequest* OutRequest, DesignImageResources::FImageOperationBudget* Budget, const TMap<FString, UTexture2D*>* TextureMap,
	FDesignPathRasterRequest* OutPathRequest)
{
	if (Node.Path.HasPaintGeometry())
	{
		FDesignPathRasterRequest Request;
		OutTexture = FDesignPathRasterizer::TryBuildRequest(Node, Request, TextureMap)
			? FDesignPathRasterizer::Rasterize(Request, nullptr, Budget) : nullptr;
		if (OutTexture && OutPathRequest) *OutPathRequest = MoveTemp(Request);
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
    if(Node.Brush.Shader.IsSet())
    {
        UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,*Node.Brush.MaterialRef.Get(FString()));
        if(!Material)return false;
        ImageWidget->SetBrushFromMaterial(Material);
        ImageWidget->SetDesiredSizeOverride(Node.Brush.Shader->Size);
        return true;
    }
	if (Node.Path.HasPaintGeometry()) return BindPathTexture(ImageWidget, Node, Budget, TextureMap);
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
