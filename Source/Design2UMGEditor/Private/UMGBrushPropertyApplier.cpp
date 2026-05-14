// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGBrushPropertyApplier.h"

#include "Design2UMGEffectMaterialBuilder.h"
#include "DesignPathRasterizer.h"
#include "DesignResourceResolver.h"
#include "UMGImageCropUtils.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"

#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/RetainerBox.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	FMargin NormalizeSerializedWidgetMargin(const FMargin& Margin)
	{
		return FUMGValueParser::ParseMargin(
			Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Margin));
	}

	FVector4 NormalizeSerializedWidgetVector4(const FVector4& Vector)
	{
		FVector4 ParsedVector = Vector;
		UMGPropertyApplierSupport::TryParseVector4(
			Design2UMGWidgetNodeAccess::SerializeWidgetVector4(Vector),
			ParsedVector);
		return ParsedVector;
	}

	FLinearColor NormalizeTypedDescriptorColor(const FLinearColor& Color)
	{
		return FUMGValueParser::ParseColor(
			Design2UMGWidgetNodeAccess::SerializeWidgetColor(Color));
	}

	TOptional<FLinearColor> NormalizeOptionalTypedDescriptorColor(const TOptional<FLinearColor>& Color)
	{
		return Color.IsSet()
			? TOptional<FLinearColor>(NormalizeTypedDescriptorColor(Color.GetValue()))
			: TOptional<FLinearColor>();
	}

	FString ResolveImageMode(const FUMGWidgetNode& Node)
	{
		return Node.WidgetStyle.ImageMode.Get(Node.Brush.Image.Mode.Get(FString()));
	}

	bool ApplyImageModeToSlateBrush(
		FSlateBrush& Brush,
		const FUMGWidgetNode& Node,
		TFunctionRef<void(UTexture2D*)> ApplyCroppedTexture)
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
				ApplyCroppedTexture);
		}

		if (Mode == TEXT("Fit"))
		{
			Brush.Tiling = ESlateBrushTileType::NoTile;
			return true;
		}

		return false;
	}

	bool ResolveProceduralTexture(const FUMGWidgetNode& Node, UObject* Outer, UTexture2D*& OutTexture)
	{
		FUMGProceduralResourceRequest Request;
		if (!Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request)
			|| Request.MaterialRef.IsEmpty())
		{
			return false;
		}

		OutTexture = DesignResourceResolver::ResolveTextureFromMaterialRef(Request, Outer);
		return OutTexture != nullptr;
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

		FLinearColor ImageColor;
		if (Node.Text.Color.IsSet())
		{
			ImageColor = NormalizeTypedDescriptorColor(Node.Text.Color.GetValue());
			Image->SetColorAndOpacity(ImageColor);
		}

		if (Node.Brush.BrushColor.IsSet())
		{
			Image->SetBrushTintColor(FSlateColor(NormalizeTypedDescriptorColor(Node.Brush.BrushColor.GetValue())));
		}

		const bool bExplicitEmptyMaterialRef = Node.Brush.MaterialRef.IsSet() && Node.Brush.MaterialRef.GetValue().IsEmpty();
		UTexture2D* GeneratedTexture = nullptr;
		if (ResolveProceduralTexture(Node, Image, GeneratedTexture))
		{
			Image->SetBrushFromTexture(GeneratedTexture);
			FSlateBrush GeneratedBrush = Image->GetBrush();
			GeneratedBrush.ImageSize = ResolveWidgetImageSize(
				Node,
				FVector2D(GeneratedTexture->GetSizeX(), GeneratedTexture->GetSizeY()));
			GeneratedBrush.Tiling = ESlateBrushTileType::NoTile;
			Image->SetBrush(GeneratedBrush);
			Image->SetDesiredSizeOverride(GeneratedBrush.ImageSize);
			return;
		}

		if (Node.Path.Geometry.IsSet())
		{
			const FString FillRule = Node.Path.FillRule.Get(TEXT("nonzero"));
			const TOptional<FLinearColor> FillColor = NormalizeOptionalTypedDescriptorColor(Node.Path.FillColor);
			const TOptional<FLinearColor> StrokeColor = NormalizeOptionalTypedDescriptorColor(Node.Path.StrokeColor);
			const float StrokeThickness = Node.Path.StrokeThickness.Get(0.0f);
			const FString StrokeCap = Node.Path.StrokeCap.Get(TEXT("butt"));
			const FString StrokeJoin = Node.Path.StrokeJoin.Get(TEXT("miter"));
			const int32 PathPixelWidth = FMath::Max(1, FMath::RoundToInt(Node.WidgetStyle.WidthOverride.Get(64.0f)));
			const int32 PathPixelHeight = FMath::Max(1, FMath::RoundToInt(Node.WidgetStyle.HeightOverride.Get(64.0f)));
			constexpr int32 PathSupersample = 2;

			if (UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
				Node.Path.Geometry.GetValue(),
				PathPixelWidth * PathSupersample,
				PathPixelHeight * PathSupersample,
				FillColor,
				FillRule,
				StrokeColor,
				StrokeThickness,
				StrokeCap,
				StrokeJoin))
			{
				Image->SetBrushFromTexture(Texture);
				FSlateBrush PathBrush = Image->GetBrush();
				PathBrush.ImageSize = FVector2D(PathPixelWidth, PathPixelHeight);
				PathBrush.Tiling = ESlateBrushTileType::NoTile;
				Image->SetBrush(PathBrush);
				Image->SetDesiredSizeOverride(FVector2D(PathPixelWidth, PathPixelHeight));
				return;
			}
		}

		const FString TexturePath = bExplicitEmptyMaterialRef ? FString() : Node.Brush.Image.TexturePath.Get(Node.Brush.TexturePath);
		if (!TexturePath.IsEmpty())
		{
			if (UTexture2D* Texture = DesignResourceResolver::LoadTextureFromPath(TexturePath, DesignResourceResolver::ELoadMode::LoadThenFind))
			{
				Image->SetBrushFromTexture(Texture);
			}
		}

		FSlateBrush Brush = Image->GetBrush();
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

		bBrushModified = ApplyImageModeToSlateBrush(Brush, Node, [&Image, &Brush, &bBrushModified](UTexture2D* Cropped)
		{
			Image->SetBrushFromTexture(Cropped);
			Brush = Image->GetBrush();
			bBrushModified = false;
		}) || bBrushModified;

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

	void ApplyBorderProps(UWidget* Widget, const FUMGWidgetNode& Node)
	{
		UBorder* Border = CastChecked<UBorder>(Widget);
		Border->SetPadding(FMargin(0.0f));
		FSlateBrush Brush = Border->Background;
		bool bBrushChanged = false;

		const bool bExplicitEmptyMaterialRef = Node.Brush.MaterialRef.IsSet() && Node.Brush.MaterialRef.GetValue().IsEmpty();
		UTexture2D* GeneratedTexture = nullptr;
		if (ResolveProceduralTexture(Node, Border, GeneratedTexture))
		{
			Brush.SetResourceObject(GeneratedTexture);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.ImageSize = FVector2D(GeneratedTexture->GetSizeX(), GeneratedTexture->GetSizeY());
			bBrushChanged = true;
		}
		else
		{
			const FString TexturePath = bExplicitEmptyMaterialRef ? FString() : Node.Brush.Image.TexturePath.Get(Node.Brush.TexturePath);
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

		const bool bHasOutlineCornerRadius = Node.Brush.OutlineCornerRadius >= 0.0f;
		const bool bHasCornerRadii = Node.Brush.HasCornerRadii();
		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && !bHasOutlineCornerRadius && !bHasCornerRadii)
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bBrushChanged = true;
		}

		if (bHasOutlineCornerRadius)
		{
			if (Node.Brush.OutlineCornerRadius >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				const float Radius = FMath::Max(0.0f, Node.Brush.OutlineCornerRadius);
				Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		if (bHasCornerRadii)
		{
			const FVector4 CornerRadii = NormalizeSerializedWidgetVector4(Node.Brush.CornerRadii);
			const float MaxRadius = FMath::Max3(CornerRadii.X, CornerRadii.Y, FMath::Max(CornerRadii.Z, CornerRadii.W));
			if (MaxRadius >= 999.0f)
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

		if (Node.Brush.OutlineColor.IsSet())
		{
			Brush.OutlineSettings.Color = FSlateColor(NormalizeTypedDescriptorColor(Node.Brush.OutlineColor.GetValue()));
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

		bBrushChanged = ApplyImageModeToSlateBrush(Brush, Node, [&Brush](UTexture2D* Cropped)
		{
			Brush.SetResourceObject(Cropped);
		}) || bBrushChanged;

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
			Border->SetPadding(NormalizeSerializedWidgetMargin(Node.WidgetStyle.Padding.GetValue()));
		}

		if (Node.WidgetStyle.StrokePadding.IsSet())
		{
			const FMargin Current = Border->GetPadding();
			const FMargin StrokePadding = NormalizeSerializedWidgetMargin(Node.WidgetStyle.StrokePadding.GetValue());
			Border->SetPadding(FMargin(
				Current.Left + StrokePadding.Left,
				Current.Top + StrokePadding.Top,
				Current.Right + StrokePadding.Right,
				Current.Bottom + StrokePadding.Bottom));
		}
	}

	static UMaterialInterface* ResolveRetainerEffectMaterial(const FString& EffectMaterialRef)
	{
		if (EffectMaterialRef.IsEmpty()) return nullptr;
		if (EffectMaterialRef.Equals(TEXT("d2u.retainer.foreground_blur"), ESearchCase::CaseSensitive))
			return Design2UMGEffectMaterialBuilder::GetOrCreateForegroundBlurRetainerMaterial();
		if (EffectMaterialRef.Equals(TEXT("d2u.retainer.inner_shadow"), ESearchCase::CaseSensitive))
			return Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial();
		if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, *EffectMaterialRef))
			return Loaded;
		return FindObject<UMaterialInterface>(nullptr, *EffectMaterialRef);
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
			RB->SetEffectMaterial(ResolveRetainerEffectMaterial(Effect.EffectMaterial.GetValue()));
		if (Effect.RenderingPhase.IsSet())
		{
			const FUMGEffectData::FRenderingPhaseData& RenderingPhase = Effect.RenderingPhase.GetValue();
			RB->SetRenderingPhase(RenderingPhase.Phase, FMath::Max(1, RenderingPhase.PhaseCount));
		}
		RB->TakeWidget();
		if (UMaterialInstanceDynamic* DynamicEffect = RB->GetEffectMaterial())
		{
			if (Effect.EffectBlurUVStep.IsSet())
			{
				const FVector2D& BlurUVStep = Effect.EffectBlurUVStep.GetValue();
				DynamicEffect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(BlurUVStep.X, BlurUVStep.Y, 0.0f, 0.0f));
			}
			if (Effect.EffectColor.IsSet())
			{
				const FLinearColor EffectColor = NormalizeTypedDescriptorColor(Effect.EffectColor.GetValue());
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowColor"), EffectColor);
				DynamicEffect->SetScalarParameterValue(TEXT("InnerShadowStrength"), EffectColor.A);
			}
			if (Effect.EffectOffset.IsSet())
			{
				const FVector2D& EffectOffset = Effect.EffectOffset.GetValue();
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(EffectOffset.X, EffectOffset.Y, 0.0f, 0.0f));
			}
		}
	}

	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect)
	{
		if (Blur && Effect.BlurStrength.IsSet())
			Blur->SetBlurStrength(Effect.BlurStrength.GetValue());
	}
}
