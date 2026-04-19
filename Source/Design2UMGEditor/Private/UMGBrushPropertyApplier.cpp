// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGBrushPropertyApplier.h"

#include "Design2UMGEffectMaterialBuilder.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterizer.h"
#include "DesignResourceResolver.h"
#include "ProceduralShapeGenerator.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGPropertyKeys.h"

#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/RetainerBox.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	using namespace UMGPropertyApplierSupport;

	bool ApplyImageModeToSlateBrush(
		FSlateBrush& Brush,
		const FUMGPropertyBag& Properties,
		TFunctionRef<void(UTexture2D*)> ApplyCroppedTexture)
	{
		FString Mode;
		if (!TryGetProp(Properties, UMGPropertyKeys::ImageMode, Mode))
		{
			return false;
		}

		if (Mode == TEXT("Crop"))
		{
			FString TransformStr;
			if (!TryGetProp(Properties, UMGPropertyKeys::ImageTransform, TransformStr))
			{
				return false;
			}

			TArray<FString> Parts;
			TransformStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() != 6)
			{
				return false;
			}

			const float A = FCString::Atof(*Parts[0]);
			const float B = FCString::Atof(*Parts[1]);
			const float TX = FCString::Atof(*Parts[2]);
			const float C = FCString::Atof(*Parts[3]);
			const float D = FCString::Atof(*Parts[4]);
			const float TY = FCString::Atof(*Parts[5]);

			if (FMath::IsNearlyZero(B) && FMath::IsNearlyZero(C) && A > KINDA_SMALL_NUMBER && D > KINDA_SMALL_NUMBER)
			{
				Brush.SetUVRegion(FBox2f(FVector2f(TX, TY), FVector2f(TX + A, TY + D)));
				return true;
			}

			if (!FMath::IsNearlyZero(B) || !FMath::IsNearlyZero(C))
			{
				UTexture2D* SrcTex = Cast<UTexture2D>(Brush.GetResourceObject());
				float CropWidth = 64.0f;
				float CropHeight = 64.0f;
				Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, CropWidth);
				Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, CropHeight);
				const int32 CropW = FMath::Max(1, FMath::RoundToInt(CropWidth));
				const int32 CropH = FMath::Max(1, FMath::RoundToInt(CropHeight));
				if (UTexture2D* Cropped = FProceduralShapeGenerator::ApplyAffineCrop(SrcTex, A, B, TX, C, D, TY, CropW, CropH))
				{
					ApplyCroppedTexture(Cropped);
					return true;
				}
			}

			return false;
		}

		if (Mode == TEXT("Fit"))
		{
			Brush.Tiling = ESlateBrushTileType::NoTile;
			return true;
		}

		return false;
	}
}

namespace UMGBrushPropertyApplier
{
	using namespace UMGPropertyApplierSupport;

	void ApplyImageProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UImage* Image = CastChecked<UImage>(Widget);
		FString Value;
		FLinearColor ImageColor;
		if (Properties.TryGetColor(UMGPropertyKeys::Color, ImageColor) || Properties.TryGetColor(UMGPropertyKeys::ColorAndOpacity, ImageColor))
		{
			Image->SetColorAndOpacity(ImageColor);
		}

		FLinearColor BrushTint;
		if (Properties.TryGetColor(UMGPropertyKeys::BrushColor, BrushTint))
		{
			Image->SetBrushTintColor(FSlateColor(BrushTint));
		}
		if (TryGetProp(Properties, UMGPropertyKeys::MaterialRef, Value))
		{
			if (UTexture2D* GeneratedTex = DesignResourceResolver::ResolveTextureFromMaterialRef(Value, Properties, Image))
			{
				Image->SetBrushFromTexture(GeneratedTex);
				FSlateBrush GeneratedBrush = Image->GetBrush();
				float DesiredWidth = static_cast<float>(GeneratedTex->GetSizeX());
				float DesiredHeight = static_cast<float>(GeneratedTex->GetSizeY());
				Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, DesiredWidth);
				Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, DesiredHeight);
				GeneratedBrush.ImageSize = FVector2D(DesiredWidth, DesiredHeight);
				GeneratedBrush.Tiling = ESlateBrushTileType::NoTile;
				Image->SetBrush(GeneratedBrush);
				Image->SetDesiredSizeOverride(GeneratedBrush.ImageSize);
				return;
			}
		}

		FString Geometry;
		if (TryGetProp(Properties, UMGPropertyKeys::PathGeometry, Geometry))
		{
			FString FillRuleStr;
			if (!TryGetProp(Properties, UMGPropertyKeys::PathFillRule, FillRuleStr))
			{
				FillRuleStr = TEXT("nonzero");
			}
			TOptional<FLinearColor> FillColor;
			FLinearColor ParsedFillColor;
			if (Properties.TryGetColor(UMGPropertyKeys::PathFillColor, ParsedFillColor))
			{
				FillColor = ParsedFillColor;
			}

			TOptional<FLinearColor> StrokeColor;
			FLinearColor ParsedStrokeColor;
			if (Properties.TryGetColor(UMGPropertyKeys::PathStrokeColor, ParsedStrokeColor))
			{
				StrokeColor = ParsedStrokeColor;
			}

			float StrokeThickness = 0.0f;
			Properties.TryGetFloat(UMGPropertyKeys::PathStrokeThickness, StrokeThickness);

			FString StrokeCap = TEXT("butt");
			TryGetProp(Properties, UMGPropertyKeys::PathStrokeCap, StrokeCap);

			FString StrokeJoin = TEXT("miter");
			TryGetProp(Properties, UMGPropertyKeys::PathStrokeJoin, StrokeJoin);

			float PathWidth = 64.0f;
			float PathHeight = 64.0f;
			Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, PathWidth);
			Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, PathHeight);
			const int32 PathPixelWidth = FMath::Max(1, FMath::RoundToInt(PathWidth));
			const int32 PathPixelHeight = FMath::Max(1, FMath::RoundToInt(PathHeight));
			constexpr int32 PathSupersample = 2;

			if (UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
				Geometry,
				PathPixelWidth * PathSupersample,
				PathPixelHeight * PathSupersample,
				FillColor,
				FillRuleStr,
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

		if (TryGetProp(Properties, UMGPropertyKeys::TexturePath, Value))
		{
			if (UTexture2D* Texture = DesignResourceResolver::LoadTextureFromPath(Value, DesignResourceResolver::ELoadMode::LoadThenFind))
			{
				Image->SetBrushFromTexture(Texture);
			}
		}

		FSlateBrush Brush = Image->GetBrush();
		bool bBrushModified = false;
		float WidthOverride = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::WidthOverride, WidthOverride))
		{
			Brush.ImageSize.X = WidthOverride;
			bBrushModified = true;
		}
		float HeightOverride = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::HeightOverride, HeightOverride))
		{
			Brush.ImageSize.Y = HeightOverride;
			bBrushModified = true;
		}

		bBrushModified = ApplyImageModeToSlateBrush(Brush, Properties, [&Image, &Brush, &bBrushModified](UTexture2D* Cropped)
		{
			Image->SetBrushFromTexture(Cropped);
			Brush = Image->GetBrush();
			bBrushModified = false;
		}) || bBrushModified;

		if (bBrushModified)
		{
			if (Brush.Tiling == ESlateBrushTileType::Both && !HasProp(Properties, UMGPropertyKeys::ImageMode))
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

	void ApplyBorderProps(UWidget* Widget, const FUMGPropertyBag& Properties)
	{
		UBorder* Border = CastChecked<UBorder>(Widget);
		Border->SetPadding(FMargin(0.0f));
		FSlateBrush Brush = Border->Background;
		bool bBrushChanged = false;
		FString Value;
		if (TryGetProp(Properties, UMGPropertyKeys::TexturePath, Value))
		{
			if (UTexture2D* Texture = DesignResourceResolver::LoadTextureFromPath(Value, DesignResourceResolver::ELoadMode::LoadThenFind))
			{
				Brush.SetResourceObject(Texture);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
				bBrushChanged = true;
			}
		}

		if (TryGetProp(Properties, UMGPropertyKeys::MaterialRef, Value))
		{
			if (UTexture2D* GeneratedTex = DesignResourceResolver::ResolveTextureFromMaterialRef(Value, Properties, Border))
			{
				Brush.SetResourceObject(GeneratedTex);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(GeneratedTex->GetSizeX(), GeneratedTex->GetSizeY());
				bBrushChanged = true;
			}
		}

		ESlateBrushDrawType::Type DrawAsType;
		if (TryGetProp(Properties, UMGPropertyKeys::DrawAs, Value) && TryParseDrawAs(Value, DrawAsType))
		{
			Brush.DrawAs = DrawAsType;
			bBrushChanged = true;
		}

		const bool bHasOutlineCornerRadius = HasProp(Properties, UMGPropertyKeys::OutlineCornerRadius);
		const bool bHasCornerRadii = HasProp(Properties, UMGPropertyKeys::CornerRadii);
		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && !bHasOutlineCornerRadius && !bHasCornerRadii)
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bBrushChanged = true;
		}

		float OutlineCornerRadius = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::OutlineCornerRadius, OutlineCornerRadius))
		{
			if (OutlineCornerRadius >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				const float Radius = FMath::Max(0.0f, OutlineCornerRadius);
				Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		FVector4 CornerRadii;
		if (TryGetProp(Properties, UMGPropertyKeys::CornerRadii, Value) && TryParseVector4(Value, CornerRadii))
		{
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

		float OutlineThickness = 0.0f;
		if (Properties.TryGetFloat(UMGPropertyKeys::OutlineThickness, OutlineThickness))
		{
			Brush.OutlineSettings.Width = FMath::Max(0.0f, OutlineThickness);
			bBrushChanged = true;
		}

		FLinearColor OutlineColor;
		if (Properties.TryGetColor(UMGPropertyKeys::OutlineColor, OutlineColor))
		{
			Brush.OutlineSettings.Color = FSlateColor(OutlineColor);
			bBrushChanged = true;
		}

		FLinearColor BorderColor;
		if (Properties.TryGetColor(UMGPropertyKeys::BrushColor, BorderColor))
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
			Border->SetBrushColor(BorderColor);
		}

		bBrushChanged = ApplyImageModeToSlateBrush(Brush, Properties, [&Brush](UTexture2D* Cropped)
		{
			Brush.SetResourceObject(Cropped);
		}) || bBrushChanged;

		if (bBrushChanged)
		{
			Border->SetBrush(Brush);
		}

		FLinearColor ContentColor;
		if (Properties.TryGetColor(UMGPropertyKeys::ContentColor, ContentColor))
		{
			Border->SetContentColorAndOpacity(ContentColor);
		}

		FMargin BorderPadding;
		if (Properties.TryGetMargin(UMGPropertyKeys::Padding, BorderPadding))
		{
			Border->SetPadding(BorderPadding);
		}

		FMargin StrokePadding;
		if (Properties.TryGetMargin(UMGPropertyKeys::StrokePadding, StrokePadding))
		{
			const FMargin Current = Border->GetPadding();
			Border->SetPadding(FMargin(
				Current.Left + StrokePadding.Left,
				Current.Top + StrokePadding.Top,
				Current.Right + StrokePadding.Right,
				Current.Bottom + StrokePadding.Bottom));
		}
	}

	void ApplyTypedBorderBrush(UBorder* Border, const FUMGVisualBrushData& BrushData)
	{
		FSlateBrush Brush = Border->Background;
		bool bChanged = false;

		if (BrushData.bHasDrawAs)
		{
			Brush.DrawAs = BrushData.DrawAs;
			bChanged = true;
		}

		if (BrushData.OutlineCornerRadius >= 0)
		{
			if (BrushData.OutlineCornerRadius >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				Brush.OutlineSettings.CornerRadii = FVector4(
					BrushData.OutlineCornerRadius,
					BrushData.OutlineCornerRadius,
					BrushData.OutlineCornerRadius,
					BrushData.OutlineCornerRadius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bChanged = true;
		}

		if (BrushData.HasCornerRadii())
		{
			const float MaxRadius = FMath::Max3(
				BrushData.CornerRadii.X,
				BrushData.CornerRadii.Y,
				FMath::Max(BrushData.CornerRadii.Z, BrushData.CornerRadii.W));
			if (MaxRadius >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				Brush.OutlineSettings.CornerRadii = BrushData.CornerRadii;
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bChanged = true;
		}

		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && BrushData.OutlineCornerRadius < 0 && !BrushData.HasCornerRadii())
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bChanged = true;
		}

		if (BrushData.bHasOutlineThickness)
		{
			Brush.OutlineSettings.Width = FMath::Max(0.0f, BrushData.OutlineThickness);
			bChanged = true;
		}

		if (BrushData.bHasOutlineColor)
		{
			Brush.OutlineSettings.Color = FSlateColor(BrushData.OutlineColor);
			bChanged = true;
		}

		if (bChanged)
		{
			Border->Background = Brush;
		}

		if (BrushData.bHasBrushColor)
		{
			Border->SetBrushColor(BrushData.BrushColor);
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

	void ApplyRetainerBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		URetainerBox* RB = CastChecked<URetainerBox>(W);
		bool bRetainRendering = false;
		if (P.TryGetBool(UMGPropertyKeys::RetainRendering, bRetainRendering)) RB->SetRetainRendering(bRetainRendering);
		FString TextureParameterValue;
		if (TryGetProp(P, UMGPropertyKeys::TextureParameter, TextureParameterValue))
			RB->SetTextureParameter(FName(*TextureParameterValue));
		FString EffectMaterialRef;
		if (TryGetProp(P, UMGPropertyKeys::EffectMaterial, EffectMaterialRef))
		{
			if (UMaterialInterface* Mat = ResolveRetainerEffectMaterial(EffectMaterialRef))
				RB->SetEffectMaterial(Mat);
		}
		int32 Phase = RB->GetPhase();
		int32 PhaseCount = RB->GetPhaseCount();
		bool bHasPhase = P.TryGetInt(UMGPropertyKeys::Phase, Phase);
		if (P.TryGetInt(UMGPropertyKeys::PhaseCount, PhaseCount))
		{
			PhaseCount = FMath::Max(1, PhaseCount);
			bHasPhase = true;
		}
		if (bHasPhase) RB->SetRenderingPhase(Phase, PhaseCount);
		RB->TakeWidget();
		if (UMaterialInstanceDynamic* DynamicEffect = RB->GetEffectMaterial())
		{
			FVector2D BlurUVStep = FVector2D::ZeroVector;
			if (P.TryGetVector2D(UMGPropertyKeys::EffectBlurUVStep, BlurUVStep))
				DynamicEffect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(BlurUVStep.X, BlurUVStep.Y, 0.0f, 0.0f));
			FLinearColor InnerShadowColor = FLinearColor::Transparent;
			if (P.TryGetColor(UMGPropertyKeys::EffectColor, InnerShadowColor))
			{
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowColor"), InnerShadowColor);
				DynamicEffect->SetScalarParameterValue(TEXT("InnerShadowStrength"), InnerShadowColor.A);
			}
			FVector2D InnerShadowOffset = FVector2D::ZeroVector;
			if (P.TryGetVector2D(UMGPropertyKeys::EffectOffset, InnerShadowOffset))
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(InnerShadowOffset.X, InnerShadowOffset.Y, 0.0f, 0.0f));
		}
	}

	void ApplyTypedRetainerBoxProps(URetainerBox* RB, const FUMGEffectData& Effect)
	{
		if (!RB) return;
		if (Effect.bHasRetainRendering) RB->SetRetainRendering(Effect.bRetainRendering);
		if (Effect.bHasTextureParameter || !Effect.TextureParameter.IsEmpty())
			RB->SetTextureParameter(FName(*Effect.TextureParameter));
		if (Effect.bHasEffectMaterial || !Effect.EffectMaterial.IsEmpty())
			RB->SetEffectMaterial(ResolveRetainerEffectMaterial(Effect.EffectMaterial));
		if (Effect.bHasRenderingPhase)
			RB->SetRenderingPhase(Effect.Phase, FMath::Max(1, Effect.PhaseCount));
		RB->TakeWidget();
		if (UMaterialInstanceDynamic* DynamicEffect = RB->GetEffectMaterial())
		{
			if (Effect.bHasEffectBlurUVStep)
				DynamicEffect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(Effect.EffectBlurUVStep.X, Effect.EffectBlurUVStep.Y, 0.0f, 0.0f));
			if (Effect.bHasEffectColor)
			{
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowColor"), Effect.EffectColor);
				DynamicEffect->SetScalarParameterValue(TEXT("InnerShadowStrength"), Effect.EffectColor.A);
			}
			if (Effect.bHasEffectOffset)
				DynamicEffect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(Effect.EffectOffset.X, Effect.EffectOffset.Y, 0.0f, 0.0f));
		}
	}

	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect)
	{
		if (Blur && Effect.bHasBlurStrength)
			Blur->SetBlurStrength(Effect.BlurStrength);
	}
}
