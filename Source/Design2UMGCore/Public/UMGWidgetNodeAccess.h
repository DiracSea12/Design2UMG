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

	inline FString SerializeWidgetVector4(const FVector4& Value)
	{
		return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.X, Value.Y, Value.Z, Value.W);
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

	struct FTypedWidgetStringDescriptor
	{
		const TCHAR* Key = nullptr;
		bool (*TryGetValue)(const FUMGWidgetNode& Node, FString& OutValue) = nullptr;
	};

	struct FTypedWidgetDescriptorRange
	{
		const FTypedWidgetStringDescriptor* Data = nullptr;
		int32 Count = 0;
	};

	template <typename VisitorType>
	inline void ForEachTypedWidgetPropertyKeyInDescriptors(
		const FTypedWidgetDescriptorRange Range,
		VisitorType&& Visitor,
		TSet<FString>& SeenKeys)
	{
		for (int32 Index = 0; Index < Range.Count; ++Index)
		{
			const FString KeyString(Range.Data[Index].Key);
			if (SeenKeys.Contains(KeyString))
			{
				continue;
			}

			SeenKeys.Add(KeyString);
			Visitor(Range.Data[Index].Key);
		}
	}

	inline bool TryGetTypedWidgetStringFromDescriptors(
		const FUMGWidgetNode& Node,
		const FString& KeyString,
		FString& OutValue,
		const FTypedWidgetDescriptorRange Range)
	{
		for (int32 Index = 0; Index < Range.Count; ++Index)
		{
			const FTypedWidgetStringDescriptor& Descriptor = Range.Data[Index];
			if (KeyString == Descriptor.Key && Descriptor.TryGetValue != nullptr && Descriptor.TryGetValue(Node, OutValue))
			{
				return true;
			}
		}

		return false;
	}

#define DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(KeyLiteral, ConditionExpr, ValueExpr) \
	{ \
		KeyLiteral, \
		[](const FUMGWidgetNode& Node, FString& OutValue) -> bool \
		{ \
			if (!(ConditionExpr)) \
			{ \
				return false; \
			} \
			OutValue = (ValueExpr); \
			return true; \
		} \
	}

#define DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(KeyLiteral, Body) \
	{ \
		KeyLiteral, \
		[](const FUMGWidgetNode& Node, FString& OutValue) -> bool Body \
	}

	inline FTypedWidgetDescriptorRange GetTextStyleTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontSize, Node.TextStyle.FontSize > 0.0f, SerializeWidgetFloat(Node.TextStyle.FontSize)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::DesignFontSize, Node.TextStyle.SourceFontSize > 0.0f, SerializeWidgetFloat(Node.TextStyle.SourceFontSize)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::LetterSpacing, Node.TextStyle.bHasLetterSpacing, SerializeWidgetFloat(Node.TextStyle.LetterSpacing)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetWidgetStyleTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::WidthOverride, Node.WidgetStyle.WidthOverride.IsSet(), SerializeWidgetFloat(Node.WidgetStyle.WidthOverride.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HeightOverride, Node.WidgetStyle.HeightOverride.IsSet(), SerializeWidgetFloat(Node.WidgetStyle.HeightOverride.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MinDesiredWidth, Node.WidgetStyle.MinDesiredWidth.IsSet(), SerializeWidgetFloat(Node.WidgetStyle.MinDesiredWidth.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MinDesiredHeight, Node.WidgetStyle.MinDesiredHeight.IsSet(), SerializeWidgetFloat(Node.WidgetStyle.MinDesiredHeight.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Padding, Node.WidgetStyle.Padding.IsSet(), SerializeWidgetMargin(Node.WidgetStyle.Padding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::InnerSlotPadding, Node.WidgetStyle.InnerSlotPadding.IsSet(), SerializeWidgetCompactVector2D(Node.WidgetStyle.InnerSlotPadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::SlotPadding, Node.WidgetStyle.SlotPadding.IsSet(), SerializeWidgetMargin(Node.WidgetStyle.SlotPadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StrokePadding, Node.WidgetStyle.StrokePadding.IsSet(), SerializeWidgetMargin(Node.WidgetStyle.StrokePadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StrokeMargin, Node.WidgetStyle.StrokeMargin.IsSet(), SerializeWidgetMargin(Node.WidgetStyle.StrokeMargin.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageMode, Node.WidgetStyle.ImageMode.IsSet(), Node.WidgetStyle.ImageMode.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageTransform, Node.WidgetStyle.ImageTransform.IsSet(), Node.WidgetStyle.ImageTransform.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Stretch, Node.WidgetStyle.Stretch.IsSet(), Node.WidgetStyle.Stretch.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StretchDirection, Node.WidgetStyle.StretchDirection.IsSet(), Node.WidgetStyle.StretchDirection.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BackgroundColor, Node.WidgetStyle.BackgroundColor.IsSet(), SerializeWidgetColor(Node.WidgetStyle.BackgroundColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HoveredColor, Node.WidgetStyle.HoveredColor.IsSet(), SerializeWidgetColor(Node.WidgetStyle.HoveredColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PressedColor, Node.WidgetStyle.PressedColor.IsSet(), SerializeWidgetColor(Node.WidgetStyle.PressedColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::NamedSlot, Node.WidgetStyle.NamedSlot.IsSet(), Node.WidgetStyle.NamedSlot.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::SlotName, Node.WidgetStyle.SlotName.IsSet(), Node.WidgetStyle.SlotName.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Visibility, Node.WidgetStyle.Visibility.IsSet(), Node.WidgetStyle.Visibility.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ToolTipText, Node.WidgetStyle.ToolTipText.IsSet(), Node.WidgetStyle.ToolTipText.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::IsEnabled, Node.WidgetStyle.IsEnabled.IsSet(), SerializeWidgetBool(Node.WidgetStyle.IsEnabled.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetTextTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Text, Node.Text.bHasText, Node.Text.Text),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RichTextStyleRows, Node.Text.bHasRichTextStyleRows, Node.Text.RichTextStyleRowsJson),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HintText, Node.Text.bHasHintText, Node.Text.HintText),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontFamily, Node.Text.bHasFontFamily, Node.Text.FontFamily),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TypefaceFontName, Node.Text.bHasTypefaceFontName, Node.Text.TypefaceFontName),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontStyle, Node.Text.bHasFontStyle, Node.Text.FontStyle),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Justification, Node.Text.bHasJustification, Node.Text.Justification),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::AutoWrapText, Node.Text.bHasAutoWrapText, SerializeWidgetBool(Node.Text.bAutoWrapText)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::WrapTextAt, Node.Text.bHasWrapTextAt, SerializeWidgetFloat(Node.Text.WrapTextAt)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::LineHeightPercentage, Node.Text.bHasLineHeightPercentage, SerializeWidgetFloat(Node.Text.LineHeightPercentage)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Underline, Node.Text.bHasUnderline, SerializeWidgetBool(Node.Text.bUnderline)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Strikethrough, Node.Text.bHasStrikethrough, SerializeWidgetBool(Node.Text.bStrikethrough)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Href, Node.Text.bHasHref, Node.Text.Href),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextAlignVertical, Node.Text.bHasTextAlignVertical, Node.Text.TextAlignVertical),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::IconName, Node.Text.bHasIconName, Node.Text.IconName),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Color, Node.Text.bHasColor, SerializeWidgetColor(Node.Text.Color)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetBrushTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BrushColor, Node.Brush.bHasBrushColor, SerializeWidgetColor(Node.Brush.BrushColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineColor, Node.Brush.bHasOutlineColor, SerializeWidgetColor(Node.Brush.OutlineColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::DrawAs, {
				if (!Node.Brush.bHasDrawAs)
				{
					return false;
				}
				OutValue = SerializeDrawAs(Node.Brush.DrawAs);
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineCornerRadius, Node.Brush.OutlineCornerRadius >= 0.0f, SerializeWidgetFloat(Node.Brush.OutlineCornerRadius)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::CornerRadii, Node.Brush.HasCornerRadii(), SerializeWidgetVector4(Node.Brush.CornerRadii)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineThickness, Node.Brush.bHasOutlineThickness, SerializeWidgetFloat(Node.Brush.OutlineThickness)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialRef, {
				if (!Node.Brush.Gradient.HasAny())
				{
					return false;
				}
				OutValue = GetGradientMaterialRef(Node.Brush.Gradient);
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MaterialRef,
				Node.Brush.bHasMaterialRef || !Node.Brush.MaterialRef.IsEmpty(),
				Node.Brush.MaterialRef),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialParams, {
				if (!Node.Brush.Gradient.HasAny())
				{
					return false;
				}
				OutValue = SerializeGradientMaterialParams(Node.Brush.Gradient);
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MaterialParams,
				Node.Brush.bHasMaterialParams || !Node.Brush.MaterialParams.IsEmpty(),
				Node.Brush.MaterialParams),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TexturePath, Node.Brush.Image.bHasTexturePath, Node.Brush.Image.TexturePath),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TexturePath, !Node.Brush.TexturePath.IsEmpty(), Node.Brush.TexturePath),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageMode, Node.Brush.Image.bHasMode, Node.Brush.Image.Mode),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageTransform, Node.Brush.Image.bHasTransform, SerializeImageTransform(Node.Brush.Image.Transform)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetEffectTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BlurStrength, Node.Effect.bHasBlurStrength, SerializeWidgetFloat(Node.Effect.BlurStrength)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RetainRendering, Node.Effect.bHasRetainRendering, SerializeWidgetBool(Node.Effect.bRetainRendering)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectMaterial,
				Node.Effect.bHasEffectMaterial || !Node.Effect.EffectMaterial.IsEmpty(),
				Node.Effect.EffectMaterial),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextureParameter,
				Node.Effect.bHasTextureParameter || !Node.Effect.TextureParameter.IsEmpty(),
				Node.Effect.TextureParameter),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectBlurUVStep, Node.Effect.bHasEffectBlurUVStep, SerializeWidgetVector2D(Node.Effect.EffectBlurUVStep)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectColor, Node.Effect.bHasEffectColor, SerializeWidgetColor(Node.Effect.EffectColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectOffset, Node.Effect.bHasEffectOffset, SerializeWidgetVector2D(Node.Effect.EffectOffset)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Phase, Node.Effect.bHasRenderingPhase, FString::FromInt(Node.Effect.Phase)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PhaseCount, Node.Effect.bHasRenderingPhase, FString::FromInt(Node.Effect.PhaseCount)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetPathTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathGeometry, Node.Path.bHasGeometry, Node.Path.Geometry),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillRule, Node.Path.bHasFillRule, Node.Path.FillRule),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillColor, Node.Path.bHasFillColor, SerializeWidgetColor(Node.Path.FillColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeColor, Node.Path.bHasStrokeColor, SerializeWidgetColor(Node.Path.StrokeColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeThickness, Node.Path.bHasStrokeThickness, SerializeWidgetFloat(Node.Path.StrokeThickness)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeCap, Node.Path.bHasStrokeCap, Node.Path.StrokeCap),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeJoin, Node.Path.bHasStrokeJoin, Node.Path.StrokeJoin),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetShadowTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowOffset, Node.Shadow.bHasShadowOffset, SerializeWidgetCompactVector2D(Node.Shadow.ShadowOffset)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowColorAndOpacity, Node.Shadow.bHasShadowColorAndOpacity, SerializeWidgetColor(Node.Shadow.ShadowColorAndOpacity)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowBlur, Node.Shadow.bHasShadowBlur, SerializeWidgetFloat(Node.Shadow.ShadowBlur)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BoxShadowCount, Node.Shadow.BoxShadows.Num() > 0, FString::FromInt(Node.Shadow.BoxShadows.Num())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetRenderTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Clipping, Node.Render.bHasClipping, Node.Render.Clipping),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderOpacity, Node.Render.bHasRenderOpacity, SerializeWidgetFloat(Node.Render.RenderOpacity)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderAngle, Node.Render.bHasRenderAngle, SerializeWidgetFloat(Node.Render.RenderAngle)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformScale, Node.Render.bHasRenderTransformScale, SerializeWidgetVector2D(Node.Render.RenderTransformScale)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformPivot, Node.Render.bHasRenderTransformPivot, SerializeWidgetVector2D(Node.Render.RenderTransformPivot)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderScale, Node.Render.bHasRenderScale, SerializeWidgetVector2D(Node.Render.RenderScale)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTranslation, Node.Render.bHasRenderTranslation, SerializeWidgetVector2D(Node.Render.RenderTranslation)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetWidgetStateTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ActiveWidgetIndex, Node.WidgetState.bHasActiveWidgetIndex, FString::FromInt(Node.WidgetState.ActiveWidgetIndex)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	inline FTypedWidgetDescriptorRange GetScrollBoxTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Orientation, Node.ScrollBox.bHasOrientation, Node.ScrollBox.Orientation),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ScrollBarVisibility, Node.ScrollBox.bHasScrollBarVisibility, Node.ScrollBox.ScrollBarVisibility),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	template <typename VisitorType>
	inline void ForEachStaticTypedWidgetPropertyKey(VisitorType&& Visitor)
	{
		TSet<FString> SeenKeys;
		ForEachTypedWidgetPropertyKeyInDescriptors(GetTextStyleTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetWidgetStyleTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetTextTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetBrushTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetEffectTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetPathTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetShadowTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetRenderTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetWidgetStateTypedWidgetDescriptors(), Visitor, SeenKeys);
		ForEachTypedWidgetPropertyKeyInDescriptors(GetScrollBoxTypedWidgetDescriptors(), Visitor, SeenKeys);
	}

	inline bool IsTypedWidgetPropertyKey(const FString& Key)
	{
		static const TSet<FString> TypedKeys = []()
		{
			TSet<FString> Result;
			ForEachStaticTypedWidgetPropertyKey([&Result](const TCHAR* TypedKey)
			{
				Result.Add(TypedKey);
			});
			return Result;
		}();
		return TypedKeys.Contains(Key) || Key.StartsWith(TEXT("BoxShadow_"));
	}

	inline bool TryGetTextStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetTextStyleTypedWidgetDescriptors());
	}

	inline bool TryGetWidgetStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetWidgetStyleTypedWidgetDescriptors());
	}

	inline bool TryGetTextTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetTextTypedWidgetDescriptors());
	}

	inline bool TryGetBrushTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetBrushTypedWidgetDescriptors());
	}

	inline bool TryGetEffectTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetEffectTypedWidgetDescriptors());
	}

	inline bool TryGetPathTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetPathTypedWidgetDescriptors());
	}

	inline bool TryGetShadowTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		if (TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetShadowTypedWidgetDescriptors()))
		{
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
		return false;
	}

	inline bool TryGetRenderTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetRenderTypedWidgetDescriptors());
	}

	inline bool TryGetWidgetStateTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetWidgetStateTypedWidgetDescriptors());
	}

	inline bool TryGetScrollBoxTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
	{
		return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetScrollBoxTypedWidgetDescriptors());
	}

	inline bool TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		const FString KeyString(Key);
		return TryGetTextStyleTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetWidgetStyleTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetTextTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetBrushTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetEffectTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetPathTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetShadowTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetRenderTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetWidgetStateTypedWidgetString(Node, KeyString, OutValue)
			|| TryGetScrollBoxTypedWidgetString(Node, KeyString, OutValue);
	}

#undef DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR
#undef DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM

	inline bool TryGetWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		// Typed empty strings are valid payloads and must block fallback to legacy Properties.
		return TryGetTypedWidgetString(Node, Key, OutValue);
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

		return false;
	}

	inline bool TryGetWidgetInt(const FUMGWidgetNode& Node, const TCHAR* Key, int32& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return LexTryParseString(OutValue, *TypedValue);
		}

		return false;
	}

	inline bool TryGetWidgetFloat(const FUMGWidgetNode& Node, const TCHAR* Key, float& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return LexTryParseString(OutValue, *TypedValue);
		}

		return false;
	}

	inline bool TryGetWidgetVector2D(const FUMGWidgetNode& Node, const TCHAR* Key, FVector2D& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseVector2D(TypedValue);
			return true;
		}

		return false;
	}

	inline bool TryGetWidgetVector4(const FUMGWidgetNode& Node, const TCHAR* Key, FVector4& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			return TryParseWidgetVector4String(TypedValue, OutValue);
		}

		return false;
	}

	inline bool TryGetWidgetMargin(const FUMGWidgetNode& Node, const TCHAR* Key, FMargin& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseMargin(TypedValue);
			return true;
		}

		return false;
	}

	inline bool TryGetWidgetColor(const FUMGWidgetNode& Node, const TCHAR* Key, FLinearColor& OutValue)
	{
		FString TypedValue;
		if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
		{
			OutValue = FUMGValueParser::ParseColor(TypedValue);
			return true;
		}

		return false;
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
		ForEachStaticTypedWidgetPropertyKey([&Result, &Node](const TCHAR* Key)
		{
			AddTypedWidgetPropertyIfPresent(Result, Node, Key);
		});
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
}
