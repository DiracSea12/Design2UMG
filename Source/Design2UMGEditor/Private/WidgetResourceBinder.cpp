// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "WidgetResourceBinder.h"
#include "DesignResourceResolver.h"
#include "Design2UMGLog.h"
#include "UMGImageCropUtils.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"

FWidgetResourceBinder::EProceduralTextureResult FWidgetResourceBinder::TryResolveProceduralTexture(
	const FUMGWidgetNode& Node,
	UObject* Outer,
	UTexture2D*& OutTexture,
	FUMGProceduralResourceRequest* OutRequest)
{
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

	OutTexture = DesignResourceResolver::ResolveTextureFromMaterialRef(ResourceRequest, Outer);
	return OutTexture != nullptr ? EProceduralTextureResult::Resolved : EProceduralTextureResult::UnknownShape;
}

bool FWidgetResourceBinder::BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
{
	// --- material (procedural shape) path ---
	UTexture2D* ProceduralTexture = nullptr;
	FUMGProceduralResourceRequest ResourceRequest;
	switch (TryResolveProceduralTexture(Node, ImageWidget, ProceduralTexture, &ResourceRequest))
	{
	case EProceduralTextureResult::Resolved:
	{
		UTexture2D* Texture = ProceduralTexture;
		ImageWidget->SetBrushFromTexture(Texture);
		FSlateBrush Brush = ImageWidget->GetBrush();
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ImageWidget->SetBrush(Brush);
		ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		ImageWidget->SynchronizeProperties();
		return true;
	}
	case EProceduralTextureResult::EmptyMaterialRef:
		return false;
	case EProceduralTextureResult::UnknownShape:
		UE_LOG(LogDesign2UMG, Warning, TEXT("Image '%s' has unknown procedural shape type '%s'; falling back to TexturePath."), *Node.Name, *ResourceRequest.MaterialRef);
		break;
	case EProceduralTextureResult::NoRequest:
	default:
		break;
	}

	// --- texture fallback path ---
	const FString TexPath = Node.Brush.Image.TexturePath.Get(Node.Brush.TexturePath);
	const FString TrimmedPath = TexPath.TrimStartAndEnd();
	if (TrimmedPath.IsEmpty())
	{
		return false;
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
		return false;
	}

	const FSlateBrush OldBrush = ImageWidget->GetBrush();
	const FVector2D OldImageSize = OldBrush.ImageSize;
	const FBox2f OldUVRegion = OldBrush.GetUVRegion();

	ImageWidget->SetBrushFromTexture(Texture);
	FSlateBrush Brush = ImageWidget->GetBrush();
	Brush.Tiling = ESlateBrushTileType::NoTile;

	const bool bHadSizeOverride = Node.WidgetStyle.WidthOverride.IsSet() || Node.WidgetStyle.HeightOverride.IsSet();
	Brush.ImageSize = bHadSizeOverride ? OldImageSize : FVector2D(Texture->GetSizeX(), Texture->GetSizeY());

	if (OldUVRegion.GetSize().GetMax() > KINDA_SMALL_NUMBER)
	{
		Brush.SetUVRegion(OldUVRegion);
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
				Brush = ImageWidget->GetBrush();
			});
	}

	ImageWidget->SetBrush(Brush);
	ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
	ImageWidget->SynchronizeProperties();
	return true;
}
