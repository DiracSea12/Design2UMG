// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGValueParser.h"

namespace Design2UMGWidgetNodeAccess
{
	inline FString SerializeWidgetFloat(const float Value)
	{
		return FString::SanitizeFloat(Value);
	}

	inline FString SerializeWidgetBool(const bool Value)
	{
		return Value ? TEXT("true") : TEXT("false");
	}

	inline FString SerializeWidgetVector2D(const FVector2D& Value)
	{
		return FString::Printf(TEXT("%.6f,%.6f"), Value.X, Value.Y);
	}

	inline FString SerializeWidgetCompactVector2D(const FVector2D& Value)
	{
		return FString::Printf(TEXT("%.1f,%.1f"), Value.X, Value.Y);
	}

	inline FString SerializeWidgetMargin(const FMargin& Value)
	{
		return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.Left, Value.Top, Value.Right, Value.Bottom);
	}

	inline FString SerializeWidgetColor(const FLinearColor& Value)
	{
		return FString::Printf(TEXT("#%s"), *Value.ToFColor(true).ToHex());
	}

	inline FString SerializeImageTransform(const FUMGVisualBrushData::FImageTransformData& Value)
	{
		return FString::Printf(
			TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
			Value.A,
			Value.B,
			Value.TX,
			Value.C,
			Value.D,
			Value.TY);
	}

	inline FString SerializeDrawAs(const ESlateBrushDrawType::Type DrawAs)
	{
		switch (DrawAs)
		{
		case ESlateBrushDrawType::NoDrawType:
			return TEXT("NoDrawType");
		case ESlateBrushDrawType::Box:
			return TEXT("Box");
		case ESlateBrushDrawType::Border:
			return TEXT("Border");
		case ESlateBrushDrawType::Image:
			return TEXT("Image");
		case ESlateBrushDrawType::RoundedBox:
			return TEXT("RoundedBox");
		default:
			return FString();
		}
	}

	inline FString GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient)
	{
		if (Gradient.bHasType && Gradient.Type.Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
		{
			return TEXT("radialgradient");
		}
		if (Gradient.bHasType && Gradient.Type.Equals(TEXT("Angular"), ESearchCase::IgnoreCase))
		{
			return TEXT("angulargradient");
		}
		return TEXT("gradientlinear");
	}

	inline FString SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient)
	{
		if (Gradient.Stops.Num() == 0)
		{
			return FString();
		}

		const FUMGVisualBrushData::FGradientStopData& StartStop = Gradient.Stops[0];
		const FUMGVisualBrushData::FGradientStopData& EndStop = Gradient.Stops.Last();
		const FString StartColor = SerializeWidgetColor(StartStop.Color);
		const FString EndColor = SerializeWidgetColor(EndStop.Color);
		const float Rotation = Gradient.bHasRotation ? Gradient.Rotation : 0.0f;
		const float CenterX = Gradient.bHasCenter ? Gradient.Center.X : 0.5f;
		const float CenterY = Gradient.bHasCenter ? Gradient.Center.Y : 0.5f;
		const float SizeWidth = Gradient.bHasSize ? Gradient.Size.X : 1.0f;
		const float SizeHeight = Gradient.bHasSize ? Gradient.Size.Y : 1.0f;
		const bool bIsRadial = Gradient.bHasType && Gradient.Type.Equals(TEXT("Radial"), ESearchCase::IgnoreCase);

		const FString RadialParams = FString::Printf(
			TEXT("\"CenterX\":%.4f,\"CenterY\":%.4f,\"SizeWidth\":%.4f,\"SizeHeight\":%.4f,\"Rotation\":%.4f"),
			CenterX,
			CenterY,
			SizeWidth,
			SizeHeight,
			Rotation);

		if (Gradient.Stops.Num() > 2)
		{
			FString Stops = bIsRadial
				? FString::Printf(TEXT("{%s,\"Stops\":["), *RadialParams)
				: FString::Printf(TEXT("{\"Rotation\":%.4f,\"Stops\":["), Rotation);
			for (int32 Index = 0; Index < Gradient.Stops.Num(); ++Index)
			{
				if (Index > 0)
				{
					Stops += TEXT(",");
				}
				Stops += FString::Printf(
					TEXT("{\"Pos\":%.4f,\"Color\":\"%s\"}"),
					Gradient.Stops[Index].Position,
					*SerializeWidgetColor(Gradient.Stops[Index].Color));
			}
			Stops += TEXT("]}");
			return Stops;
		}

		if (bIsRadial)
		{
			return FString::Printf(
				TEXT("{%s,\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"),
				*RadialParams,
				*StartColor,
				*EndColor);
		}

		return FString::Printf(
			TEXT("{\"Rotation\":%.4f,\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"),
			Rotation,
			*StartColor,
			*EndColor);
	}

	inline bool IsTypedWidgetPropertyKey(const FString& Key)
	{
		static const TSet<FString> TypedKeys = {
			UMGPropertyKeys::FontSize,
			UMGPropertyKeys::DesignFontSize,
			UMGPropertyKeys::LetterSpacing,
			UMGPropertyKeys::WidthOverride,
			UMGPropertyKeys::HeightOverride,
			UMGPropertyKeys::MinDesiredWidth,
			UMGPropertyKeys::MinDesiredHeight,
			UMGPropertyKeys::Padding,
			UMGPropertyKeys::InnerSlotPadding,
			UMGPropertyKeys::SlotPadding,
			UMGPropertyKeys::StrokePadding,
			UMGPropertyKeys::StrokeMargin,
			UMGPropertyKeys::ImageMode,
			UMGPropertyKeys::ImageTransform,
			UMGPropertyKeys::Stretch,
			UMGPropertyKeys::StretchDirection,
			UMGPropertyKeys::BackgroundColor,
			UMGPropertyKeys::HoveredColor,
			UMGPropertyKeys::PressedColor,
			UMGPropertyKeys::NamedSlot,
			UMGPropertyKeys::SlotName,
			UMGPropertyKeys::Visibility,
			UMGPropertyKeys::ToolTipText,
			UMGPropertyKeys::IsEnabled,
			UMGPropertyKeys::Text,
			UMGPropertyKeys::HintText,
			UMGPropertyKeys::FontFamily,
			UMGPropertyKeys::TypefaceFontName,
			UMGPropertyKeys::FontStyle,
			UMGPropertyKeys::Justification,
			UMGPropertyKeys::AutoWrapText,
			UMGPropertyKeys::WrapTextAt,
			UMGPropertyKeys::LineHeightPercentage,
			UMGPropertyKeys::Underline,
			UMGPropertyKeys::Strikethrough,
			UMGPropertyKeys::Href,
			UMGPropertyKeys::TextAlignVertical,
			UMGPropertyKeys::IconName,
			UMGPropertyKeys::Color,
			UMGPropertyKeys::BrushColor,
			UMGPropertyKeys::OutlineColor,
			UMGPropertyKeys::DrawAs,
			UMGPropertyKeys::OutlineCornerRadius,
			UMGPropertyKeys::CornerRadii,
			UMGPropertyKeys::OutlineThickness,
			UMGPropertyKeys::MaterialRef,
			UMGPropertyKeys::MaterialParams,
			UMGPropertyKeys::TexturePath,
			UMGPropertyKeys::BlurStrength,
			UMGPropertyKeys::RetainRendering,
			UMGPropertyKeys::EffectMaterial,
			UMGPropertyKeys::TextureParameter,
			UMGPropertyKeys::EffectBlurUVStep,
			UMGPropertyKeys::EffectColor,
			UMGPropertyKeys::EffectOffset,
			UMGPropertyKeys::Phase,
			UMGPropertyKeys::PhaseCount,
			UMGPropertyKeys::PathGeometry,
			UMGPropertyKeys::PathFillRule,
			UMGPropertyKeys::PathFillColor,
			UMGPropertyKeys::PathStrokeColor,
			UMGPropertyKeys::PathStrokeThickness,
			UMGPropertyKeys::PathStrokeCap,
			UMGPropertyKeys::PathStrokeJoin,
			UMGPropertyKeys::ShadowOffset,
			UMGPropertyKeys::ShadowColorAndOpacity,
			UMGPropertyKeys::ShadowBlur,
			UMGPropertyKeys::Clipping,
			UMGPropertyKeys::RenderOpacity,
			UMGPropertyKeys::RenderAngle,
			UMGPropertyKeys::RenderTransformPivot,
			UMGPropertyKeys::RenderScale,
			UMGPropertyKeys::RenderTransformScale,
			UMGPropertyKeys::RenderTranslation,
			UMGPropertyKeys::ActiveWidgetIndex,
			UMGPropertyKeys::BoxShadowCount,
		};

		return TypedKeys.Contains(Key) || Key.StartsWith(TEXT("BoxShadow_"));
	}

	inline bool TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		const FString KeyString(Key);

		if (KeyString == UMGPropertyKeys::FontSize && Node.TextStyle.FontSize > 0.0f)
		{
			OutValue = FString::SanitizeFloat(Node.TextStyle.FontSize);
			return true;
		}
		if (KeyString == UMGPropertyKeys::DesignFontSize && Node.TextStyle.SourceFontSize > 0.0f)
		{
			OutValue = FString::SanitizeFloat(Node.TextStyle.SourceFontSize);
			return true;
		}
		if (KeyString == UMGPropertyKeys::LetterSpacing && Node.TextStyle.bHasLetterSpacing)
		{
			OutValue = FString::SanitizeFloat(Node.TextStyle.LetterSpacing);
			return true;
		}
		if (KeyString == UMGPropertyKeys::WidthOverride && Node.WidgetStyle.bHasWidthOverride)
		{
			OutValue = SerializeWidgetFloat(Node.WidgetStyle.WidthOverride);
			return true;
		}
		if (KeyString == UMGPropertyKeys::HeightOverride && Node.WidgetStyle.bHasHeightOverride)
		{
			OutValue = SerializeWidgetFloat(Node.WidgetStyle.HeightOverride);
			return true;
		}
		if (KeyString == UMGPropertyKeys::MinDesiredWidth && Node.WidgetStyle.bHasMinDesiredWidth)
		{
			OutValue = SerializeWidgetFloat(Node.WidgetStyle.MinDesiredWidth);
			return true;
		}
		if (KeyString == UMGPropertyKeys::MinDesiredHeight && Node.WidgetStyle.bHasMinDesiredHeight)
		{
			OutValue = SerializeWidgetFloat(Node.WidgetStyle.MinDesiredHeight);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Padding && Node.WidgetStyle.bHasPadding)
		{
			OutValue = SerializeWidgetMargin(Node.WidgetStyle.Padding);
			return true;
		}
		if (KeyString == UMGPropertyKeys::InnerSlotPadding && Node.WidgetStyle.bHasInnerSlotPadding)
		{
			OutValue = SerializeWidgetCompactVector2D(Node.WidgetStyle.InnerSlotPadding);
			return true;
		}
		if (KeyString == UMGPropertyKeys::SlotPadding && Node.WidgetStyle.bHasSlotPadding)
		{
			OutValue = SerializeWidgetMargin(Node.WidgetStyle.SlotPadding);
			return true;
		}
		if (KeyString == UMGPropertyKeys::StrokePadding && Node.WidgetStyle.bHasStrokePadding)
		{
			OutValue = SerializeWidgetMargin(Node.WidgetStyle.StrokePadding);
			return true;
		}
		if (KeyString == UMGPropertyKeys::StrokeMargin && Node.WidgetStyle.bHasStrokeMargin)
		{
			OutValue = SerializeWidgetMargin(Node.WidgetStyle.StrokeMargin);
			return true;
		}
		if (KeyString == UMGPropertyKeys::ImageMode && Node.WidgetStyle.bHasImageMode)
		{
			OutValue = Node.WidgetStyle.ImageMode;
			return true;
		}
		if (KeyString == UMGPropertyKeys::ImageTransform && Node.WidgetStyle.bHasImageTransform)
		{
			OutValue = Node.WidgetStyle.ImageTransform;
			return true;
		}
		if (KeyString == UMGPropertyKeys::Stretch && Node.WidgetStyle.bHasStretch)
		{
			OutValue = Node.WidgetStyle.Stretch;
			return true;
		}
		if (KeyString == UMGPropertyKeys::StretchDirection && Node.WidgetStyle.bHasStretchDirection)
		{
			OutValue = Node.WidgetStyle.StretchDirection;
			return true;
		}
		if (KeyString == UMGPropertyKeys::BackgroundColor && Node.WidgetStyle.bHasBackgroundColor)
		{
			OutValue = SerializeWidgetColor(Node.WidgetStyle.BackgroundColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::HoveredColor && Node.WidgetStyle.bHasHoveredColor)
		{
			OutValue = SerializeWidgetColor(Node.WidgetStyle.HoveredColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PressedColor && Node.WidgetStyle.bHasPressedColor)
		{
			OutValue = SerializeWidgetColor(Node.WidgetStyle.PressedColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::NamedSlot && Node.WidgetStyle.bHasNamedSlot)
		{
			OutValue = Node.WidgetStyle.NamedSlot;
			return true;
		}
		if (KeyString == UMGPropertyKeys::SlotName && Node.WidgetStyle.bHasSlotName)
		{
			OutValue = Node.WidgetStyle.SlotName;
			return true;
		}
		if (KeyString == UMGPropertyKeys::Visibility && Node.WidgetStyle.bHasVisibility)
		{
			OutValue = Node.WidgetStyle.Visibility;
			return true;
		}
		if (KeyString == UMGPropertyKeys::ToolTipText && Node.WidgetStyle.bHasToolTipText)
		{
			OutValue = Node.WidgetStyle.ToolTipText;
			return true;
		}
		if (KeyString == UMGPropertyKeys::IsEnabled && Node.WidgetStyle.bHasIsEnabled)
		{
			OutValue = SerializeWidgetBool(Node.WidgetStyle.bIsEnabled);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Text && Node.Text.bHasText)
		{
			OutValue = Node.Text.Text;
			return true;
		}
		if (KeyString == UMGPropertyKeys::HintText && Node.Text.bHasHintText)
		{
			OutValue = Node.Text.HintText;
			return true;
		}
		if (KeyString == UMGPropertyKeys::FontFamily && Node.Text.bHasFontFamily)
		{
			OutValue = Node.Text.FontFamily;
			return true;
		}
		if (KeyString == UMGPropertyKeys::TypefaceFontName && Node.Text.bHasTypefaceFontName)
		{
			OutValue = Node.Text.TypefaceFontName;
			return true;
		}
		if (KeyString == UMGPropertyKeys::FontStyle && Node.Text.bHasFontStyle)
		{
			OutValue = Node.Text.FontStyle;
			return true;
		}
		if (KeyString == UMGPropertyKeys::Justification && Node.Text.bHasJustification)
		{
			OutValue = Node.Text.Justification;
			return true;
		}
		if (KeyString == UMGPropertyKeys::AutoWrapText && Node.Text.bHasAutoWrapText)
		{
			OutValue = SerializeWidgetBool(Node.Text.bAutoWrapText);
			return true;
		}
		if (KeyString == UMGPropertyKeys::WrapTextAt && Node.Text.bHasWrapTextAt)
		{
			OutValue = SerializeWidgetFloat(Node.Text.WrapTextAt);
			return true;
		}
		if (KeyString == UMGPropertyKeys::LineHeightPercentage && Node.Text.bHasLineHeightPercentage)
		{
			OutValue = SerializeWidgetFloat(Node.Text.LineHeightPercentage);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Underline && Node.Text.bHasUnderline)
		{
			OutValue = SerializeWidgetBool(Node.Text.bUnderline);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Strikethrough && Node.Text.bHasStrikethrough)
		{
			OutValue = SerializeWidgetBool(Node.Text.bStrikethrough);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Href && Node.Text.bHasHref)
		{
			OutValue = Node.Text.Href;
			return true;
		}
		if (KeyString == UMGPropertyKeys::TextAlignVertical && Node.Text.bHasTextAlignVertical)
		{
			OutValue = Node.Text.TextAlignVertical;
			return true;
		}
		if (KeyString == UMGPropertyKeys::IconName && Node.Text.bHasIconName)
		{
			OutValue = Node.Text.IconName;
			return true;
		}
		if (KeyString == UMGPropertyKeys::Color && Node.Text.bHasColor)
		{
			OutValue = SerializeWidgetColor(Node.Text.Color);
			return true;
		}
		if (KeyString == UMGPropertyKeys::BrushColor && Node.Brush.bHasBrushColor)
		{
			OutValue = SerializeWidgetColor(Node.Brush.BrushColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::OutlineColor && Node.Brush.bHasOutlineColor)
		{
			OutValue = SerializeWidgetColor(Node.Brush.OutlineColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::DrawAs && Node.Brush.bHasDrawAs)
		{
			OutValue = SerializeDrawAs(Node.Brush.DrawAs);
			return !OutValue.IsEmpty();
		}
		if (KeyString == UMGPropertyKeys::OutlineCornerRadius && Node.Brush.OutlineCornerRadius >= 0.0f)
		{
			OutValue = FString::SanitizeFloat(Node.Brush.OutlineCornerRadius);
			return true;
		}
		if (KeyString == UMGPropertyKeys::CornerRadii && Node.Brush.HasCornerRadii())
		{
			OutValue = FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
				Node.Brush.CornerRadii.X,
				Node.Brush.CornerRadii.Y,
				Node.Brush.CornerRadii.Z,
				Node.Brush.CornerRadii.W);
			return true;
		}
		if (KeyString == UMGPropertyKeys::OutlineThickness && Node.Brush.bHasOutlineThickness)
		{
			OutValue = FString::SanitizeFloat(Node.Brush.OutlineThickness);
			return true;
		}
		if (KeyString == UMGPropertyKeys::MaterialRef && Node.Brush.Gradient.HasAny())
		{
			OutValue = GetGradientMaterialRef(Node.Brush.Gradient);
			return !OutValue.IsEmpty();
		}
		if (KeyString == UMGPropertyKeys::MaterialRef
			&& (Node.Brush.bHasMaterialRef || !Node.Brush.MaterialRef.IsEmpty()))
		{
			OutValue = Node.Brush.MaterialRef;
			return true;
		}
		if (KeyString == UMGPropertyKeys::MaterialParams && Node.Brush.Gradient.HasAny())
		{
			OutValue = SerializeGradientMaterialParams(Node.Brush.Gradient);
			return !OutValue.IsEmpty();
		}
		if (KeyString == UMGPropertyKeys::MaterialParams
			&& (Node.Brush.bHasMaterialParams || !Node.Brush.MaterialParams.IsEmpty()))
		{
			OutValue = Node.Brush.MaterialParams;
			return true;
		}
		if (KeyString == UMGPropertyKeys::TexturePath && Node.Brush.Image.bHasTexturePath)
		{
			OutValue = Node.Brush.Image.TexturePath;
			return true;
		}
		if (KeyString == UMGPropertyKeys::TexturePath && !Node.Brush.TexturePath.IsEmpty())
		{
			OutValue = Node.Brush.TexturePath;
			return true;
		}
		if (KeyString == UMGPropertyKeys::ImageMode && Node.Brush.Image.bHasMode)
		{
			OutValue = Node.Brush.Image.Mode;
			return true;
		}
		if (KeyString == UMGPropertyKeys::ImageTransform && Node.Brush.Image.bHasTransform)
		{
			OutValue = SerializeImageTransform(Node.Brush.Image.Transform);
			return true;
		}
		if (KeyString == UMGPropertyKeys::BlurStrength && Node.Effect.bHasBlurStrength)
		{
			OutValue = SerializeWidgetFloat(Node.Effect.BlurStrength);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RetainRendering && Node.Effect.bHasRetainRendering)
		{
			OutValue = SerializeWidgetBool(Node.Effect.bRetainRendering);
			return true;
		}
		if (KeyString == UMGPropertyKeys::EffectMaterial
			&& (Node.Effect.bHasEffectMaterial || !Node.Effect.EffectMaterial.IsEmpty()))
		{
			OutValue = Node.Effect.EffectMaterial;
			return true;
		}
		if (KeyString == UMGPropertyKeys::TextureParameter
			&& (Node.Effect.bHasTextureParameter || !Node.Effect.TextureParameter.IsEmpty()))
		{
			OutValue = Node.Effect.TextureParameter;
			return true;
		}
		if (KeyString == UMGPropertyKeys::EffectBlurUVStep && Node.Effect.bHasEffectBlurUVStep)
		{
			OutValue = SerializeWidgetVector2D(Node.Effect.EffectBlurUVStep);
			return true;
		}
		if (KeyString == UMGPropertyKeys::EffectColor && Node.Effect.bHasEffectColor)
		{
			OutValue = SerializeWidgetColor(Node.Effect.EffectColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::EffectOffset && Node.Effect.bHasEffectOffset)
		{
			OutValue = SerializeWidgetVector2D(Node.Effect.EffectOffset);
			return true;
		}
		if (KeyString == UMGPropertyKeys::Phase && Node.Effect.bHasRenderingPhase)
		{
			OutValue = FString::FromInt(Node.Effect.Phase);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PhaseCount && Node.Effect.bHasRenderingPhase)
		{
			OutValue = FString::FromInt(Node.Effect.PhaseCount);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathGeometry && Node.Path.bHasGeometry)
		{
			OutValue = Node.Path.Geometry;
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathFillRule && Node.Path.bHasFillRule)
		{
			OutValue = Node.Path.FillRule;
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathFillColor && Node.Path.bHasFillColor)
		{
			OutValue = SerializeWidgetColor(Node.Path.FillColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathStrokeColor && Node.Path.bHasStrokeColor)
		{
			OutValue = SerializeWidgetColor(Node.Path.StrokeColor);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathStrokeThickness && Node.Path.bHasStrokeThickness)
		{
			OutValue = SerializeWidgetFloat(Node.Path.StrokeThickness);
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathStrokeCap && Node.Path.bHasStrokeCap)
		{
			OutValue = Node.Path.StrokeCap;
			return true;
		}
		if (KeyString == UMGPropertyKeys::PathStrokeJoin && Node.Path.bHasStrokeJoin)
		{
			OutValue = Node.Path.StrokeJoin;
			return true;
		}
		if (KeyString == UMGPropertyKeys::ShadowOffset && Node.Shadow.bHasShadowOffset)
		{
			OutValue = FString::Printf(TEXT("%.1f,%.1f"), Node.Shadow.ShadowOffset.X, Node.Shadow.ShadowOffset.Y);
			return true;
		}
		if (KeyString == UMGPropertyKeys::ShadowColorAndOpacity && Node.Shadow.bHasShadowColorAndOpacity)
		{
			OutValue = SerializeWidgetColor(Node.Shadow.ShadowColorAndOpacity);
			return true;
		}
		if (KeyString == UMGPropertyKeys::ShadowBlur && Node.Shadow.bHasShadowBlur)
		{
			OutValue = SerializeWidgetFloat(Node.Shadow.ShadowBlur);
			return true;
		}
		if (KeyString == UMGPropertyKeys::BoxShadowCount && Node.Shadow.BoxShadows.Num() > 0)
		{
			OutValue = FString::FromInt(Node.Shadow.BoxShadows.Num());
			return true;
		}
		if (KeyString.StartsWith(TEXT("BoxShadow_")))
		{
			TArray<FString> Parts;
			KeyString.ParseIntoArray(Parts, TEXT("_"));
			if (Parts.Num() == 3)
			{
				const int32 Index = FCString::Atoi(*Parts[1]);
				if (Node.Shadow.BoxShadows.IsValidIndex(Index))
				{
					const FUMGBoxShadowLayerData& Layer = Node.Shadow.BoxShadows[Index];
					const FString& Field = Parts[2];
					if (Field == TEXT("OffsetX"))
					{
						OutValue = SerializeWidgetFloat(Layer.Offset.X);
						return true;
					}
					if (Field == TEXT("OffsetY"))
					{
						OutValue = SerializeWidgetFloat(Layer.Offset.Y);
						return true;
					}
					if (Field == TEXT("Blur"))
					{
						OutValue = SerializeWidgetFloat(Layer.Blur);
						return true;
					}
					if (Field == TEXT("Spread"))
					{
						OutValue = SerializeWidgetFloat(Layer.Spread);
						return true;
					}
					if (Field == TEXT("Color"))
					{
						OutValue = SerializeWidgetColor(Layer.Color);
						return true;
					}
				}
			}
		}
		if (KeyString == UMGPropertyKeys::Clipping && Node.Render.bHasClipping)
		{
			OutValue = Node.Render.Clipping;
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderOpacity && Node.Render.bHasRenderOpacity)
		{
			OutValue = SerializeWidgetFloat(Node.Render.RenderOpacity);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderAngle && Node.Render.bHasRenderAngle)
		{
			OutValue = SerializeWidgetFloat(Node.Render.RenderAngle);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderTransformScale && Node.Render.bHasRenderTransformScale)
		{
			OutValue = SerializeWidgetVector2D(Node.Render.RenderTransformScale);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderTransformPivot && Node.Render.bHasRenderTransformPivot)
		{
			OutValue = SerializeWidgetVector2D(Node.Render.RenderTransformPivot);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderScale && Node.Render.bHasRenderScale)
		{
			OutValue = SerializeWidgetVector2D(Node.Render.RenderScale);
			return true;
		}
		if (KeyString == UMGPropertyKeys::RenderTranslation && Node.Render.bHasRenderTranslation)
		{
			OutValue = SerializeWidgetVector2D(Node.Render.RenderTranslation);
			return true;
		}
		if (KeyString == UMGPropertyKeys::ActiveWidgetIndex && Node.WidgetState.bHasActiveWidgetIndex)
		{
			OutValue = FString::FromInt(Node.WidgetState.ActiveWidgetIndex);
			return true;
		}

		return false;
	}

	inline bool TryGetWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		// Typed empty strings are valid payloads and must block fallback to legacy Properties.
		if (TryGetTypedWidgetString(Node, Key, OutValue))
		{
			return true;
		}

		return Node.Properties.TryGetString(Key, OutValue);
	}

	inline bool TryParseWidgetVector4String(const FString& Value, FVector4& OutValue)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 4)
		{
			return false;
		}

		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;
		if (!LexTryParseString(X, *Parts[0])
			|| !LexTryParseString(Y, *Parts[1])
			|| !LexTryParseString(Z, *Parts[2])
			|| !LexTryParseString(W, *Parts[3]))
		{
			return false;
		}

		OutValue = FVector4(X, Y, Z, W);
		return true;
	}

	inline bool TryGetWidgetBool(const FUMGWidgetNode& Node, const TCHAR* Key, bool& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = TypedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || TypedValue == TEXT("1");
			return true;
		}

		return Node.Properties.TryGetBool(Key, OutValue);
	}

	inline bool TryGetWidgetInt(const FUMGWidgetNode& Node, const TCHAR* Key, int32& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return LexTryParseString(OutValue, *TypedValue);
		}

		return Node.Properties.TryGetInt(Key, OutValue);
	}

	inline bool TryGetWidgetFloat(const FUMGWidgetNode& Node, const TCHAR* Key, float& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return LexTryParseString(OutValue, *TypedValue);
		}

		return Node.Properties.TryGetFloat(Key, OutValue);
	}

	inline bool TryGetWidgetVector2D(const FUMGWidgetNode& Node, const TCHAR* Key, FVector2D& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseVector2D(TypedValue);
			return true;
		}

		return Node.Properties.TryGetVector2D(Key, OutValue);
	}

	inline bool TryGetWidgetVector4(const FUMGWidgetNode& Node, const TCHAR* Key, FVector4& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return TryParseWidgetVector4String(TypedValue, OutValue);
		}

		return Node.Properties.TryGetVector4(Key, OutValue);
	}

	inline bool TryGetWidgetMargin(const FUMGWidgetNode& Node, const TCHAR* Key, FMargin& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseMargin(TypedValue);
			return true;
		}

		return Node.Properties.TryGetMargin(Key, OutValue);
	}

	inline bool TryGetWidgetColor(const FUMGWidgetNode& Node, const TCHAR* Key, FLinearColor& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseColor(TypedValue);
			return true;
		}

		return Node.Properties.TryGetColor(Key, OutValue);
	}

	inline bool HasWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		return TryGetWidgetString(Node, Key, Value) && !Value.IsEmpty();
	}

	inline void AddTypedWidgetPropertyIfPresent(FUMGPropertyBag& Bag, const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue))
		{
			Bag.Remove(Key);
			Bag.Add(Key, TypedValue);
		}
	}

	inline FUMGPropertyBag BuildTypedPropertyBag(const FUMGWidgetNode& Node)
	{
		FUMGPropertyBag Result;
		for (const TCHAR* Key : {
			UMGPropertyKeys::FontSize,
			UMGPropertyKeys::DesignFontSize,
			UMGPropertyKeys::LetterSpacing,
			UMGPropertyKeys::WidthOverride,
			UMGPropertyKeys::HeightOverride,
			UMGPropertyKeys::MinDesiredWidth,
			UMGPropertyKeys::MinDesiredHeight,
			UMGPropertyKeys::Padding,
			UMGPropertyKeys::InnerSlotPadding,
			UMGPropertyKeys::SlotPadding,
			UMGPropertyKeys::StrokePadding,
			UMGPropertyKeys::StrokeMargin,
			UMGPropertyKeys::ImageMode,
			UMGPropertyKeys::ImageTransform,
			UMGPropertyKeys::Stretch,
			UMGPropertyKeys::StretchDirection,
			UMGPropertyKeys::BackgroundColor,
			UMGPropertyKeys::HoveredColor,
			UMGPropertyKeys::PressedColor,
			UMGPropertyKeys::NamedSlot,
			UMGPropertyKeys::SlotName,
			UMGPropertyKeys::Visibility,
			UMGPropertyKeys::ToolTipText,
			UMGPropertyKeys::IsEnabled,
			UMGPropertyKeys::Text,
			UMGPropertyKeys::HintText,
			UMGPropertyKeys::FontFamily,
			UMGPropertyKeys::TypefaceFontName,
			UMGPropertyKeys::FontStyle,
			UMGPropertyKeys::Justification,
			UMGPropertyKeys::AutoWrapText,
			UMGPropertyKeys::WrapTextAt,
			UMGPropertyKeys::LineHeightPercentage,
			UMGPropertyKeys::Underline,
			UMGPropertyKeys::Strikethrough,
			UMGPropertyKeys::Href,
			UMGPropertyKeys::TextAlignVertical,
			UMGPropertyKeys::IconName,
			UMGPropertyKeys::Color,
			UMGPropertyKeys::BrushColor,
			UMGPropertyKeys::OutlineColor,
			UMGPropertyKeys::DrawAs,
			UMGPropertyKeys::OutlineCornerRadius,
			UMGPropertyKeys::CornerRadii,
			UMGPropertyKeys::OutlineThickness,
			UMGPropertyKeys::MaterialRef,
			UMGPropertyKeys::MaterialParams,
			UMGPropertyKeys::TexturePath,
			UMGPropertyKeys::BlurStrength,
			UMGPropertyKeys::RetainRendering,
			UMGPropertyKeys::EffectMaterial,
			UMGPropertyKeys::TextureParameter,
			UMGPropertyKeys::EffectBlurUVStep,
			UMGPropertyKeys::EffectColor,
			UMGPropertyKeys::EffectOffset,
			UMGPropertyKeys::Phase,
			UMGPropertyKeys::PhaseCount,
			UMGPropertyKeys::PathGeometry,
			UMGPropertyKeys::PathFillRule,
			UMGPropertyKeys::PathFillColor,
			UMGPropertyKeys::PathStrokeColor,
			UMGPropertyKeys::PathStrokeThickness,
			UMGPropertyKeys::PathStrokeCap,
			UMGPropertyKeys::PathStrokeJoin,
			UMGPropertyKeys::ShadowOffset,
			UMGPropertyKeys::ShadowColorAndOpacity,
			UMGPropertyKeys::ShadowBlur,
			UMGPropertyKeys::BoxShadowCount,
			UMGPropertyKeys::Clipping,
			UMGPropertyKeys::RenderOpacity,
			UMGPropertyKeys::RenderAngle,
			UMGPropertyKeys::RenderTransformPivot,
			UMGPropertyKeys::RenderScale,
			UMGPropertyKeys::RenderTransformScale,
			UMGPropertyKeys::RenderTranslation,
			UMGPropertyKeys::ActiveWidgetIndex,
		})
		{
			AddTypedWidgetPropertyIfPresent(Result, Node, Key);
		}
		for (int32 Index = 0; Index < Node.Shadow.BoxShadows.Num(); ++Index)
		{
			AddTypedWidgetPropertyIfPresent(Result, Node, *FString::Printf(TEXT("BoxShadow_%d_OffsetX"), Index));
			AddTypedWidgetPropertyIfPresent(Result, Node, *FString::Printf(TEXT("BoxShadow_%d_OffsetY"), Index));
			AddTypedWidgetPropertyIfPresent(Result, Node, *FString::Printf(TEXT("BoxShadow_%d_Blur"), Index));
			AddTypedWidgetPropertyIfPresent(Result, Node, *FString::Printf(TEXT("BoxShadow_%d_Spread"), Index));
			AddTypedWidgetPropertyIfPresent(Result, Node, *FString::Printf(TEXT("BoxShadow_%d_Color"), Index));
		}
		return Result;
	}

	inline void RemoveLegacyPropertiesShadowedByTypedProperties(FUMGPropertyBag& LegacyProperties, const FUMGPropertyBag& TypedProperties)
	{
		TypedProperties.ForEachSerialized([&LegacyProperties](const FString& Key, const FString&)
		{
			LegacyProperties.Remove(Key);
		});
	}

	inline FUMGPropertyBag BuildLegacyFallbackPropertyBag(const FUMGWidgetNode& Node)
	{
		FUMGPropertyBag Result = Node.Properties;
		const FUMGPropertyBag TypedProperties = BuildTypedPropertyBag(Node);
		RemoveLegacyPropertiesShadowedByTypedProperties(Result, TypedProperties);
		return Result;
	}

	inline FUMGPropertyBag BuildEffectivePropertyBag(const FUMGWidgetNode& Node)
	{
		FUMGPropertyBag Result = BuildTypedPropertyBag(Node);
		const FUMGPropertyBag LegacyProperties = BuildLegacyFallbackPropertyBag(Node);
		LegacyProperties.ForEachSerialized([&Result](const FString& Key, const FString& Value)
		{
			Result.Add(Key, Value);
		});
		return Result;
	}
}
