// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetNodeTypedPropertyAccess.h"

#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"

namespace
{
	enum class EBrushDescriptorValueSource : uint8
	{
		None,
		GradientMaterialRef,
		ExplicitMaterialRef,
		ImageTexturePath,
		BrushTexturePath
	};

	struct FResolvedBrushDescriptorValue
	{
		FString Value;
		EBrushDescriptorValueSource Source = EBrushDescriptorValueSource::None;

		bool HasValue() const
		{
			return !Value.IsEmpty();
		}
	};

	FResolvedBrushDescriptorValue ResolveBrushMaterialRefDescriptor(const FUMGWidgetNode& Node)
	{
		if (Node.Brush.Gradient.HasAny())
		{
			return {
				Design2UMGWidgetNodeAccess::GetGradientMaterialRef(Node.Brush.Gradient),
				EBrushDescriptorValueSource::GradientMaterialRef
			};
		}

		if (Node.Brush.bHasMaterialRef || !Node.Brush.MaterialRef.IsEmpty())
		{
			return { Node.Brush.MaterialRef, EBrushDescriptorValueSource::ExplicitMaterialRef };
		}

		return {};
	}

	FResolvedBrushDescriptorValue ResolveBrushTexturePathDescriptor(const FUMGWidgetNode& Node)
	{
		if (Node.Brush.Image.bHasTexturePath && !Node.Brush.Image.TexturePath.IsEmpty())
		{
			return { Node.Brush.Image.TexturePath, EBrushDescriptorValueSource::ImageTexturePath };
		}

		if (!Node.Brush.TexturePath.IsEmpty())
		{
			return { Node.Brush.TexturePath, EBrushDescriptorValueSource::BrushTexturePath };
		}

		return {};
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
	void ForEachTypedWidgetPropertyKeyInDescriptors(
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

	bool TryGetTypedWidgetStringFromDescriptors(
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

	FTypedWidgetDescriptorRange GetTextStyleTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontSize, Node.TextStyle.FontSize > 0.0f, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.TextStyle.FontSize)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::DesignFontSize, Node.TextStyle.SourceFontSize > 0.0f, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.TextStyle.SourceFontSize)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::LetterSpacing, Node.TextStyle.LetterSpacing.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.TextStyle.LetterSpacing.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetWidgetStyleTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::WidthOverride, Node.WidgetStyle.WidthOverride.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.WidthOverride.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HeightOverride, Node.WidgetStyle.HeightOverride.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.HeightOverride.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MinDesiredWidth, Node.WidgetStyle.MinDesiredWidth.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.MinDesiredWidth.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MinDesiredHeight, Node.WidgetStyle.MinDesiredHeight.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.MinDesiredHeight.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Padding, Node.WidgetStyle.Padding.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Node.WidgetStyle.Padding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::InnerSlotPadding, Node.WidgetStyle.InnerSlotPadding.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Node.WidgetStyle.InnerSlotPadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::SlotPadding, Node.WidgetStyle.SlotPadding.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Node.WidgetStyle.SlotPadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StrokePadding, Node.WidgetStyle.StrokePadding.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Node.WidgetStyle.StrokePadding.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StrokeMargin, Node.WidgetStyle.StrokeMargin.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Node.WidgetStyle.StrokeMargin.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageMode, Node.WidgetStyle.ImageMode.IsSet(), Node.WidgetStyle.ImageMode.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageTransform, Node.WidgetStyle.ImageTransform.IsSet(), Node.WidgetStyle.ImageTransform.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Stretch, Node.WidgetStyle.Stretch.IsSet(), Node.WidgetStyle.Stretch.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::StretchDirection, Node.WidgetStyle.StretchDirection.IsSet(), Node.WidgetStyle.StretchDirection.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BackgroundColor, Node.WidgetStyle.BackgroundColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.WidgetStyle.BackgroundColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HoveredColor, Node.WidgetStyle.HoveredColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.WidgetStyle.HoveredColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PressedColor, Node.WidgetStyle.PressedColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.WidgetStyle.PressedColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::NamedSlot, Node.WidgetStyle.NamedSlot.IsSet(), Node.WidgetStyle.NamedSlot.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::SlotName, Node.WidgetStyle.SlotName.IsSet(), Node.WidgetStyle.SlotName.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Visibility, Node.WidgetStyle.Visibility.IsSet(), Node.WidgetStyle.Visibility.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ToolTipText, Node.WidgetStyle.ToolTipText.IsSet(), Node.WidgetStyle.ToolTipText.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::IsEnabled, Node.WidgetStyle.IsEnabled.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.WidgetStyle.IsEnabled.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetTextTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Text, Node.Text.bHasText, Node.Text.Text),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RichTextStyleRows, Node.Text.bHasRichTextStyleRows, Node.Text.RichTextStyleRowsJson),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HintText, Node.Text.bHasHintText, Node.Text.HintText),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontFamily, Node.Text.bHasFontFamily, Node.Text.FontFamily),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TypefaceFontName, Node.Text.bHasTypefaceFontName, Node.Text.TypefaceFontName),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontStyle, Node.Text.bHasFontStyle, Node.Text.FontStyle),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Justification, Node.Text.bHasJustification, Node.Text.Justification),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::AutoWrapText, Node.Text.bHasAutoWrapText, Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bAutoWrapText)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::WrapTextAt, Node.Text.bHasWrapTextAt, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Text.WrapTextAt)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::LineHeightPercentage, Node.Text.bHasLineHeightPercentage, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Text.LineHeightPercentage)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Underline, Node.Text.bHasUnderline, Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bUnderline)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Strikethrough, Node.Text.bHasStrikethrough, Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bStrikethrough)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Href, Node.Text.bHasHref, Node.Text.Href),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextAlignVertical, Node.Text.bHasTextAlignVertical, Node.Text.TextAlignVertical),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::IconName, Node.Text.bHasIconName, Node.Text.IconName),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Color, Node.Text.bHasColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetBrushTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BrushColor, Node.Brush.bHasBrushColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.BrushColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineColor, Node.Brush.bHasOutlineColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.OutlineColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::DrawAs, {
				if (!Node.Brush.bHasDrawAs)
				{
					return false;
				}
				OutValue = Design2UMGWidgetNodeAccess::SerializeDrawAs(Node.Brush.DrawAs);
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineCornerRadius, Node.Brush.OutlineCornerRadius >= 0.0f, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Brush.OutlineCornerRadius)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::CornerRadii, Node.Brush.HasCornerRadii(), Design2UMGWidgetNodeAccess::SerializeWidgetVector4(Node.Brush.CornerRadii)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineThickness, Node.Brush.bHasOutlineThickness, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Brush.OutlineThickness)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialRef, {
				const FResolvedBrushDescriptorValue DescriptorValue = ResolveBrushMaterialRefDescriptor(Node);
				OutValue = DescriptorValue.Value;
				return DescriptorValue.HasValue();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialParams, {
				if (!Node.Brush.Gradient.HasAny())
				{
					return false;
				}
				OutValue = Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::MaterialParams,
				Node.Brush.bHasMaterialParams || !Node.Brush.MaterialParams.IsEmpty(),
				Node.Brush.MaterialParams),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::TexturePath, {
				const FResolvedBrushDescriptorValue DescriptorValue = ResolveBrushTexturePathDescriptor(Node);
				OutValue = DescriptorValue.Value;
				return DescriptorValue.HasValue();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageMode, Node.Brush.Image.bHasMode, Node.Brush.Image.Mode),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageTransform, Node.Brush.Image.bHasTransform, Design2UMGWidgetNodeAccess::SerializeImageTransform(Node.Brush.Image.Transform)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetEffectTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BlurStrength, Node.Effect.bHasBlurStrength, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Effect.BlurStrength)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RetainRendering, Node.Effect.bHasRetainRendering, Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Effect.bRetainRendering)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectMaterial,
				Node.Effect.bHasEffectMaterial || !Node.Effect.EffectMaterial.IsEmpty(),
				Node.Effect.EffectMaterial),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextureParameter,
				Node.Effect.bHasTextureParameter || !Node.Effect.TextureParameter.IsEmpty(),
				Node.Effect.TextureParameter),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectBlurUVStep, Node.Effect.bHasEffectBlurUVStep, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Effect.EffectBlurUVStep)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectColor, Node.Effect.bHasEffectColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Effect.EffectColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectOffset, Node.Effect.bHasEffectOffset, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Effect.EffectOffset)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Phase, Node.Effect.bHasRenderingPhase, FString::FromInt(Node.Effect.Phase)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PhaseCount, Node.Effect.bHasRenderingPhase, FString::FromInt(Node.Effect.PhaseCount)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetPathTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathGeometry, Node.Path.bHasGeometry, Node.Path.Geometry),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillRule, Node.Path.bHasFillRule, Node.Path.FillRule),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillColor, Node.Path.bHasFillColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeColor, Node.Path.bHasStrokeColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeThickness, Node.Path.bHasStrokeThickness, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Path.StrokeThickness)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeCap, Node.Path.bHasStrokeCap, Node.Path.StrokeCap),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeJoin, Node.Path.bHasStrokeJoin, Node.Path.StrokeJoin),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetShadowTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowOffset, Node.Shadow.bHasShadowOffset, Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Node.Shadow.ShadowOffset)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowColorAndOpacity, Node.Shadow.bHasShadowColorAndOpacity, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Shadow.ShadowColorAndOpacity)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowBlur, Node.Shadow.bHasShadowBlur, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Shadow.ShadowBlur)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BoxShadowCount, Node.Shadow.BoxShadows.Num() > 0, FString::FromInt(Node.Shadow.BoxShadows.Num())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetRenderTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Clipping, Node.Render.bHasClipping, Node.Render.Clipping),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderOpacity, Node.Render.bHasRenderOpacity, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderOpacity)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderAngle, Node.Render.bHasRenderAngle, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderAngle)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformScale, Node.Render.bHasRenderTransformScale, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTransformScale)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformPivot, Node.Render.bHasRenderTransformPivot, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTransformPivot)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTranslation, Node.Render.bHasRenderTranslation, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTranslation)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetWidgetStateTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ActiveWidgetIndex, Node.WidgetState.bHasActiveWidgetIndex, FString::FromInt(Node.WidgetState.ActiveWidgetIndex)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetScrollBoxTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Orientation, Node.ScrollBox.bHasOrientation, Node.ScrollBox.Orientation),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ScrollBarVisibility, Node.ScrollBox.bHasScrollBarVisibility, Node.ScrollBox.ScrollBarVisibility),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

#undef DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR
#undef DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM
}

void Design2UMGWidgetNodeTypedPropertyAccess::ForEachStaticTypedWidgetPropertyKey(TFunctionRef<void(const TCHAR*)> Visitor)
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

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetTextStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetTextStyleTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetWidgetStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetWidgetStyleTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetTextTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetTextTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetBrushTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetBrushTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetEffectTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetEffectTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetPathTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetPathTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetShadowTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
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
					OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Layer.Offset.X);
					return true;
				}
				if (Field == TEXT("OffsetY"))
				{
					OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Layer.Offset.Y);
					return true;
				}
				if (Field == TEXT("Blur"))
				{
					OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Layer.Blur);
					return true;
				}
				if (Field == TEXT("Spread"))
				{
					OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Layer.Spread);
					return true;
				}
				if (Field == TEXT("Color"))
				{
					OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetColor(Layer.Color);
					return true;
				}
			}
		}
	}
	return false;
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetRenderTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetRenderTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetWidgetStateTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetWidgetStateTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetScrollBoxTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return TryGetTypedWidgetStringFromDescriptors(Node, KeyString, OutValue, GetScrollBoxTypedWidgetDescriptors());
}

bool Design2UMGWidgetNodeTypedPropertyAccess::TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
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

bool Design2UMGWidgetNodeTypedPropertyAccess::TryParseWidgetVector4String(const FString& Value, FVector4& OutValue)
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

void Design2UMGWidgetNodeTypedPropertyAccess::AddTypedWidgetPropertyIfPresent(FUMGPropertyBag& Bag, const FUMGWidgetNode& Node, const TCHAR* Key)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue))
	{
		Bag.Remove(Key);
		Bag.Add(Key, TypedValue);
	}
}
