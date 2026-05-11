// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "WidgetResourceBinder.h"
#include "DesignResourceResolver.h"
#include "Design2UMGLog.h"
#include "UMGImageCropUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"

bool FWidgetResourceBinder::BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
{
	// --- material (procedural shape) path ---
	FString MatRef;
	if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, MatRef))
	{
		if (MatRef.IsEmpty())
		{
			return false;
		}

		FUMGPropertyBag GenerationProps = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
		UTexture2D* Texture = DesignResourceResolver::ResolveTextureFromMaterialRef(MatRef, GenerationProps, ImageWidget);
		if (Texture)
		{
			ImageWidget->SetBrushFromTexture(Texture);
			FSlateBrush Brush = ImageWidget->GetBrush();
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.Tiling = ESlateBrushTileType::NoTile;
			ImageWidget->SetBrush(Brush);
			ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
			ImageWidget->SynchronizeProperties();
			return true;
		}
		UE_LOG(LogDesign2UMG, Warning, TEXT("Image '%s' 未知形状类型: %s，fallback 到 TexturePath"), *Node.Name, *MatRef);
	}

	// --- texture fallback path ---
	FString TexPath;
	Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, TexPath);
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

	float SizeOverride = 0.0f;
	const bool bHadSizeOverride =
		Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::WidthOverride, SizeOverride) ||
		Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::HeightOverride, SizeOverride);
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
