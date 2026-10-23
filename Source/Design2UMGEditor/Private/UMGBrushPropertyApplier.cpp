// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGBrushPropertyApplier.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "DesignImageResourceLimits.h"
#include "DesignPathRasterizer.h"

#include "Design2UMGEffectMaterialBuilder.h"
#include "DesignResourceResolver.h"
#include "UMGImageCropUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "WidgetResourceBinder.h"

#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/RetainerBox.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Use the serialized brush-color representation so typed colors receive the same
	// channel quantization as JSON brush properties.
	FLinearColor NormalizeTypedDescriptorColor(const FLinearColor& Color)
	{
		return FUMGValueParser::ParseColor(
			Design2UMGWidgetNodeAccess::SerializeWidgetColor(Color));
	}

	FString ResolveImageMode(const FUMGWidgetNode& Node)
	{
		return Node.Brush.Image.Mode.Get(FString());
	}

	bool ApplyImageModeToSlateBrush(
		FSlateBrush& Brush,
		const FUMGWidgetNode& Node,
		TFunctionRef<void(UTexture2D*)> ApplyCroppedTexture, DesignImageResources::FImageOperationBudget* Budget)
	{
		const FString Mode = ResolveImageMode(Node);
		if (Mode.IsEmpty())
		{
			return false;
		}

		if (Mode == TEXT("Crop"))
		{
			UMGImageCropUtils::FImageCropRequest CropRequest;
			if (!UMGImageCropUtils::TryBuildCropRequest(Node, CropRequest))
			{
				return false;
			}

			return UMGImageCropUtils::TryApplyCropToBrush(
				Brush,
				Cast<UTexture2D>(Brush.GetResourceObject()),
				CropRequest,
				ApplyCroppedTexture, Budget);
		}

		if (Mode == TEXT("Fit"))
		{
			Brush.Tiling = ESlateBrushTileType::NoTile;
			return true;
		}

		return false;
	}

	FVector2D ResolveWidgetImageSize(const FUMGWidgetNode& Node, const FVector2D& DefaultSize)
	{
		return FVector2D(
			Node.WidgetStyle.WidthOverride.Get(DefaultSize.X),
			Node.WidgetStyle.HeightOverride.Get(DefaultSize.Y));
	}
}

namespace UMGBrushPropertyApplier
{
	void ApplyImageProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UImage* Image = CastChecked<UImage>(Widget);

		if (Node.Text.Color.IsSet())
		{
			Image->SetColorAndOpacity(Node.Text.Color.GetValue());
		}

		if (Node.Brush.BrushColor.IsSet())
		{
			Image->SetBrushTintColor(FSlateColor(Node.Brush.BrushColor.GetValue()));
		}

		FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*Image);
		bool bBrushModified = false;
		if (Node.WidgetStyle.WidthOverride.IsSet())
		{
			Brush.ImageSize.X = Node.WidgetStyle.WidthOverride.GetValue();
			bBrushModified = true;
		}
		if (Node.WidgetStyle.HeightOverride.IsSet())
		{
			Brush.ImageSize.Y = Node.WidgetStyle.HeightOverride.GetValue();
			bBrushModified = true;
		}

		if (bBrushModified)
		{
			if (Brush.Tiling == ESlateBrushTileType::Both && ResolveImageMode(Node).IsEmpty())
			{
				Brush.Tiling = ESlateBrushTileType::NoTile;
			}
			Image->SetBrush(Brush);
			if (Brush.ImageSize.X > 0 && Brush.ImageSize.Y > 0)
			{
				Image->SetDesiredSizeOverride(Brush.ImageSize);
			}
		}
	}

	void ApplyBorderProps(UWidget* Widget, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget)
	{
		DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
		if (!Budget) Budget = &LocalBudget;
		UBorder* Border = CastChecked<UBorder>(Widget);
		Border->SetPadding(FMargin(0.0f));
		FSlateBrush Brush = Border->Background;
		bool bBrushChanged = false;

		const bool bExplicitEmptyMaterialRef = Node.Brush.MaterialRef.IsSet() && Node.Brush.MaterialRef.GetValue().IsEmpty();
		UTexture2D* GeneratedTexture = nullptr;
		if (FWidgetResourceBinder::TryResolveProceduralTexture(Node, Border, GeneratedTexture, nullptr, Budget)
			== FWidgetResourceBinder::EProceduralTextureResult::Resolved)
		{
			Brush.SetResourceObject(GeneratedTexture);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.ImageSize = FVector2D(GeneratedTexture->GetSizeX(), GeneratedTexture->GetSizeY());
			if (Node.Path.HasPaintGeometry())
			{
				const auto UV = PathRequest.GetCanvasUVTransform();
				Design2UMGWidgetPropertyAccess::SetUVRegion(Brush, FBox2f(FVector2f(UV.TX, UV.TY), FVector2f(UV.TX + UV.A, UV.TY + UV.D)));
				Brush.ImageSize = FVector2D(PathRequest.CanvasBounds.Size());
			}
			bBrushChanged = true;
		}
		else
		{
			const FString TexturePath = bExplicitEmptyMaterialRef ? FString() : Node.Brush.Image.TexturePath.Get(FString());
			if (!TexturePath.IsEmpty())
			{
				if (UTexture2D* Texture = DesignResourceResolver::LoadTextureFromPath(TexturePath, DesignResourceResolver::ELoadMode::LoadThenFind))
				{
					Brush.SetResourceObject(Texture);
					Brush.DrawAs = ESlateBrushDrawType::Image;
					Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
					bBrushChanged = true;
				}
			}
		}

		if (Node.Brush.DrawAs.IsSet())
		{
			Brush.DrawAs = Node.Brush.DrawAs.GetValue();
			bBrushChanged = true;
		}

		const bool bHasOutlineCornerRadius = Node.Brush.OutlineCornerRadius.IsSet();
		const bool bHasCornerRadii = Node.Brush.CornerRadii.IsSet();
		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && !bHasOutlineCornerRadius && !bHasCornerRadii)
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bBrushChanged = true;
		}

		if (bHasOutlineCornerRadius)
		{
			if (Node.Brush.OutlineCornerRadius.GetValue() >= UMGPropertyKeys::CapsuleRadiusSentinel)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				const float Radius = FMath::Max(0.0f, Node.Brush.OutlineCornerRadius.GetValue());
				Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		if (bHasCornerRadii)
		{
			const FVector4& CornerRadii = Node.Brush.CornerRadii.GetValue();
			const float MaxRadius = FMath::Max3(CornerRadii.X, CornerRadii.Y, FMath::Max(CornerRadii.Z, CornerRadii.W));
			if (MaxRadius >= UMGPropertyKeys::CapsuleRadiusSentinel)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				Brush.OutlineSettings.CornerRadii = CornerRadii;
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		if (Node.Brush.OutlineThickness.IsSet())
		{
			Brush.OutlineSettings.Width = FMath::Max(0.0f, Node.Brush.OutlineThickness.GetValue());
			bBrushChanged = true;
		}
		if (Node.Brush.Margin.IsSet())
		{
			Brush.Margin = Node.Brush.Margin.GetValue();
			bBrushChanged = true;
		}

		if (Node.Brush.OutlineColor.IsSet())
		{
			Brush.OutlineSettings.Color = FSlateColor(Node.Brush.OutlineColor.GetValue());
			bBrushChanged = true;
		}

		if (Node.Brush.BrushColor.IsSet())
		{
			if (!Brush.GetResourceObject() && Brush.DrawAs != ESlateBrushDrawType::RoundedBox)
			{
				const ESlateBrushDrawType::Type ExistingDrawAs = Brush.DrawAs;
				if (UTexture2D* WhiteTex = LoadObject<UTexture2D>(
					nullptr,
					TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")))
				{
					Brush.SetResourceObject(WhiteTex);
					if (ExistingDrawAs == ESlateBrushDrawType::NoDrawType)
					{
						Brush.DrawAs = ESlateBrushDrawType::Image;
					}
					Brush.ImageSize = FVector2D(1.0f, 1.0f);
					bBrushChanged = true;
				}
			}
		}

		if (!Node.Path.HasPaintGeometry())
		{
			bBrushChanged = ApplyImageModeToSlateBrush(Brush, Node, [&Brush](UTexture2D* Cropped)
			{
				Brush.SetResourceObject(Cropped);
			}, Budget) || bBrushChanged;
		}

		if (bBrushChanged)
		{
			Border->SetBrush(Brush);
		}

		if (Node.Brush.BrushColor.IsSet())
		{
			Border->SetBrushColor(NormalizeTypedDescriptorColor(Node.Brush.BrushColor.GetValue()));
		}

		if (Node.WidgetStyle.Padding.IsSet())
		{
			Border->SetPadding(Node.WidgetStyle.Padding.GetValue());
		}

		if (Node.WidgetStyle.StrokePadding.IsSet())
		{
			const FMargin Current = Design2UMGWidgetPropertyAccess::GetPadding(*Border);
			const FMargin StrokePadding = Node.WidgetStyle.StrokePadding.GetValue();
			Border->SetPadding(FMargin(
				Current.Left + StrokePadding.Left,
				Current.Top + StrokePadding.Top,
				Current.Right + StrokePadding.Right,
				Current.Bottom + StrokePadding.Bottom));
		}
	}

	void ApplyRetainerBoxProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		ApplyTypedRetainerBoxProps(CastChecked<URetainerBox>(Widget), Node.Effect);
	}

	void ApplyTypedRetainerBoxProps(URetainerBox* RB, const FUMGEffectData& Effect)
	{
		if (!RB) return;
		if (Effect.bRetainRendering.IsSet()) RB->SetRetainRendering(Effect.bRetainRendering.GetValue());
		if (Effect.TextureParameter.IsSet())
			RB->SetTextureParameter(FName(*Effect.TextureParameter.GetValue()));
		if (Effect.EffectMaterial.IsSet())
			RB->SetEffectMaterial(Design2UMGEffectMaterialBuilder::GetOrCreateRetainerEffectMaterial(Effect));
		if (Effect.RenderingPhase.IsSet())
		{
			const FUMGEffectData::FRenderingPhaseData& RenderingPhase = Effect.RenderingPhase.GetValue();
			RB->SetRenderingPhase(RenderingPhase.Phase, FMath::Max(1, RenderingPhase.PhaseCount));
		}
		if (!Effect.EffectMaterial.IsSet())
		{
			UMaterialInterface* Parent = const_cast<UMaterialInterface*>(Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*RB));
			if (Parent)
				RB->SetEffectMaterial(Design2UMGEffectMaterialBuilder::GetOrCreateRetainerEffectMaterial(Effect, Parent));
		}
	}

	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect)
	{
		if (Blur && Effect.BlurStrength.IsSet())
			Blur->SetBlurStrength(Effect.BlurStrength.GetValue());
	}
}
