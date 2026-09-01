// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGBrushPropertyApplier.h"

#include "Design2UMGEffectMaterialBuilder.h"
#include "DesignPathRasterDefaults.h"
#include "DesignPathRasterizer.h"
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
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Intentionally kept as a serialized-protocol round trip: automation tests
	// (WidgetResourceBinderTests border brush color, EffectsAndShadow retainer
	// inner-shadow color) anchor these two consumers to the JSON-preview color
	// value, so they must stay quantized instead of passing the typed payload
	// through at full precision.
	FLinearColor NormalizeTypedDescriptorColor(const FLinearColor& Color)
	{
		return FUMGValueParser::ParseColor(
			Design2UMGWidgetNodeAccess::SerializeWidgetColor(Color));
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

		const bool bExplicitEmptyMaterialRef = Node.Brush.MaterialRef.IsSet() && Node.Brush.MaterialRef.GetValue().IsEmpty();
		UTexture2D* GeneratedTexture = nullptr;
		if (FWidgetResourceBinder::TryResolveProceduralTexture(Node, Image, GeneratedTexture)
			== FWidgetResourceBinder::EProceduralTextureResult::Resolved)
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
			const FString FillRule = Node.Path.FillRule.Get(DesignPathRasterDefaults::FillRule);
			const TOptional<FLinearColor>& FillColor = Node.Path.FillColor;
			const TOptional<FLinearColor>& StrokeColor = Node.Path.StrokeColor;
			const float StrokeThickness = Node.Path.StrokeThickness.Get(0.0f);
			const FString StrokeCap = Node.Path.StrokeCap.Get(DesignPathRasterDefaults::StrokeCap);
			const FString StrokeJoin = Node.Path.StrokeJoin.Get(DesignPathRasterDefaults::StrokeJoin);
			const int32 PathPixelWidth = FMath::Max(1, FMath::RoundToInt(Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize))));
			const int32 PathPixelHeight = FMath::Max(1, FMath::RoundToInt(Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize))));
			constexpr int32 PathSupersample = DesignPathRasterDefaults::Supersample;

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
		if (FWidgetResourceBinder::TryResolveProceduralTexture(Node, Border, GeneratedTexture)
			== FWidgetResourceBinder::EProceduralTextureResult::Resolved)
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
			Border->SetPadding(Node.WidgetStyle.Padding.GetValue());
		}

		if (Node.WidgetStyle.StrokePadding.IsSet())
		{
			const FMargin Current = Border->GetPadding();
			const FMargin StrokePadding = Node.WidgetStyle.StrokePadding.GetValue();
			Border->SetPadding(FMargin(
				Current.Left + StrokePadding.Left,
				Current.Top + StrokePadding.Top,
				Current.Right + StrokePadding.Right,
				Current.Bottom + StrokePadding.Bottom));
		}
	}

	static UMaterialInterface* ResolveRetainerEffectMaterial(
		const FString& EffectMaterialRef,
		const FUMGEffectData* Effect = nullptr)
	{
		if (EffectMaterialRef.IsEmpty()) return nullptr;
		if (EffectMaterialRef.Equals(UMGPropertyKeys::RetainerForegroundBlurEffectRef, ESearchCase::CaseSensitive))
			return Design2UMGEffectMaterialBuilder::GetOrCreateForegroundBlurRetainerMaterial();
		if (EffectMaterialRef.Equals(UMGPropertyKeys::RetainerGeometryMaskEffectRef, ESearchCase::CaseSensitive))
		{
			if (Effect
				&& Effect->GeometryMaskShape.IsSet()
				&& Effect->GeometryMaskSize.IsSet()
				&& Effect->GeometryMaskCornerRadii.IsSet())
			{
				return Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial(
					Effect->GeometryMaskShape.GetValue(),
					Effect->GeometryMaskSize.GetValue(),
					Effect->GeometryMaskCornerRadii.GetValue());
			}
			return Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial();
		}
		if (EffectMaterialRef.Equals(UMGPropertyKeys::RetainerInnerShadowEffectRef, ESearchCase::CaseSensitive))
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
			RB->SetEffectMaterial(ResolveRetainerEffectMaterial(Effect.EffectMaterial.GetValue(), &Effect));
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
			if (Effect.GeometryMaskShape.IsSet())
			{
				DynamicEffect->SetScalarParameterValue(
					TEXT("MaskShape"),
					Effect.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::Ellipse ? 0.0f : 1.0f);
			}
			if (Effect.GeometryMaskSize.IsSet())
			{
				const FVector2D& MaskSize = Effect.GeometryMaskSize.GetValue();
				DynamicEffect->SetVectorParameterValue(TEXT("MaskSize"), FLinearColor(MaskSize.X, MaskSize.Y, 0.0f, 0.0f));
			}
			if (Effect.GeometryMaskCornerRadii.IsSet())
			{
				const FVector4& Radii = Effect.GeometryMaskCornerRadii.GetValue();
				DynamicEffect->SetVectorParameterValue(TEXT("MaskCornerRadiiTop"), FLinearColor(Radii.X, Radii.Y, 0.0f, 0.0f));
				DynamicEffect->SetVectorParameterValue(TEXT("MaskCornerRadiiBottom"), FLinearColor(Radii.Z, Radii.W, 0.0f, 0.0f));
			}
		}
	}

	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect)
	{
		if (Blur && Effect.BlurStrength.IsSet())
			Blur->SetBlurStrength(Effect.BlurStrength.GetValue());
	}
}
