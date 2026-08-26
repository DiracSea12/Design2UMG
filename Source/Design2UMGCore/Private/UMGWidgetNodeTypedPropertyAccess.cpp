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
		GradientMaterialParams,
		ExplicitMaterialParams,
		ImageTexturePath
	};

	struct FResolvedBrushDescriptorValue
	{
		FString Value;
		EBrushDescriptorValueSource Source = EBrushDescriptorValueSource::None;

		bool HasValue() const
		{
			return Source != EBrushDescriptorValueSource::None;
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

		if (Node.Brush.MaterialRef.IsSet())
		{
			return { Node.Brush.MaterialRef.GetValue(), EBrushDescriptorValueSource::ExplicitMaterialRef };
		}

		return {};
	}

	FResolvedBrushDescriptorValue ResolveBrushMaterialParamsDescriptor(const FUMGWidgetNode& Node)
	{
		if (Node.Brush.Gradient.HasAny())
		{
			const FString GradientMaterialParams =
				Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
			if (!GradientMaterialParams.IsEmpty())
			{
				return { GradientMaterialParams, EBrushDescriptorValueSource::GradientMaterialParams };
			}
		}

		if (Node.Brush.MaterialParams.IsSet())
		{
			return {
				Node.Brush.MaterialParams.GetValue(),
				EBrushDescriptorValueSource::ExplicitMaterialParams
			};
		}

		return {};
	}

	FResolvedBrushDescriptorValue ResolveBrushTexturePathDescriptor(const FUMGWidgetNode& Node)
	{
		if (Node.Brush.Image.TexturePath.IsSet())
		{
			return { Node.Brush.Image.TexturePath.GetValue(), EBrushDescriptorValueSource::ImageTexturePath };
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
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Text, Node.Text.Text.IsSet(), Node.Text.Text.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RichTextStyleRows, Node.Text.RichTextStyleRowsJson.IsSet(), Node.Text.RichTextStyleRowsJson.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::HintText, Node.Text.HintText.IsSet(), Node.Text.HintText.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontFamily, Node.Text.FontFamily.IsSet(), Node.Text.FontFamily.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TypefaceFontName, Node.Text.TypefaceFontName.IsSet(), Node.Text.TypefaceFontName.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::FontStyle, Node.Text.FontStyle.IsSet(), Node.Text.FontStyle.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Justification, Node.Text.Justification.IsSet(), Node.Text.Justification.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::AutoWrapText, Node.Text.bAutoWrapText.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bAutoWrapText.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::WrapTextAt, Node.Text.WrapTextAt.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Text.WrapTextAt.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::LineHeightPercentage, Node.Text.LineHeightPercentage.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Text.LineHeightPercentage.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Underline, Node.Text.bUnderline.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bUnderline.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Strikethrough, Node.Text.bStrikethrough.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Text.bStrikethrough.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Href, Node.Text.Href.IsSet(), Node.Text.Href.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextAlignVertical, Node.Text.TextAlignVertical.IsSet(), Node.Text.TextAlignVertical.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::IconName, Node.Text.IconName.IsSet(), Node.Text.IconName.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Color, Node.Text.Color.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetBrushTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BrushColor, Node.Brush.BrushColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.BrushColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineColor, Node.Brush.OutlineColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.OutlineColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::DrawAs, {
				if (!Node.Brush.DrawAs.IsSet())
				{
					return false;
				}
				OutValue = Design2UMGWidgetNodeAccess::SerializeDrawAs(Node.Brush.DrawAs.GetValue());
				return !OutValue.IsEmpty();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineCornerRadius, Node.Brush.OutlineCornerRadius >= 0.0f, Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Brush.OutlineCornerRadius)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::CornerRadii, Node.Brush.HasCornerRadii(), Design2UMGWidgetNodeAccess::SerializeWidgetVector4(Node.Brush.CornerRadii)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::OutlineThickness, Node.Brush.OutlineThickness.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Brush.OutlineThickness.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialRef, {
				const FResolvedBrushDescriptorValue DescriptorValue = ResolveBrushMaterialRefDescriptor(Node);
				OutValue = DescriptorValue.Value;
				return DescriptorValue.HasValue();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::MaterialParams, {
				const FResolvedBrushDescriptorValue DescriptorValue = ResolveBrushMaterialParamsDescriptor(Node);
				OutValue = DescriptorValue.Value;
				return DescriptorValue.HasValue();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR_CUSTOM(UMGPropertyKeys::TexturePath, {
				const FResolvedBrushDescriptorValue DescriptorValue = ResolveBrushTexturePathDescriptor(Node);
				OutValue = DescriptorValue.Value;
				return DescriptorValue.HasValue();
			}),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageMode, Node.Brush.Image.Mode.IsSet(), Node.Brush.Image.Mode.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ImageTransform, Node.Brush.Image.Transform.IsSet(), Design2UMGWidgetNodeAccess::SerializeImageTransform(Node.Brush.Image.Transform.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetEffectTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BlurStrength, Node.Effect.BlurStrength.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Effect.BlurStrength.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RetainRendering, Node.Effect.bRetainRendering.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetBool(Node.Effect.bRetainRendering.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectMaterial,
				Node.Effect.EffectMaterial.IsSet(),
				Node.Effect.EffectMaterial.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::TextureParameter,
				Node.Effect.TextureParameter.IsSet(),
				Node.Effect.TextureParameter.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectBlurUVStep, Node.Effect.EffectBlurUVStep.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Effect.EffectBlurUVStep.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectColor, Node.Effect.EffectColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Effect.EffectColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::EffectOffset, Node.Effect.EffectOffset.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Effect.EffectOffset.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Phase, Node.Effect.RenderingPhase.IsSet(), FString::FromInt(Node.Effect.RenderingPhase.GetValue().Phase)),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PhaseCount, Node.Effect.RenderingPhase.IsSet(), FString::FromInt(Node.Effect.RenderingPhase.GetValue().PhaseCount)),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetPathTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathGeometry, Node.Path.Geometry.IsSet(), Node.Path.Geometry.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillRule, Node.Path.FillRule.IsSet(), Node.Path.FillRule.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathFillColor, Node.Path.FillColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.FillColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeColor, Node.Path.StrokeColor.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Path.StrokeColor.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeThickness, Node.Path.StrokeThickness.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Path.StrokeThickness.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeCap, Node.Path.StrokeCap.IsSet(), Node.Path.StrokeCap.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::PathStrokeJoin, Node.Path.StrokeJoin.IsSet(), Node.Path.StrokeJoin.GetValue()),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetShadowTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowOffset, Node.Shadow.ShadowOffset.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Node.Shadow.ShadowOffset.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowColorAndOpacity, Node.Shadow.ShadowColorAndOpacity.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Shadow.ShadowColorAndOpacity.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ShadowBlur, Node.Shadow.ShadowBlur.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Shadow.ShadowBlur.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::BoxShadowCount, Node.Shadow.BoxShadows.Num() > 0, FString::FromInt(Node.Shadow.BoxShadows.Num())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetRenderTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Clipping, Node.Render.Clipping.IsSet(), Node.Render.Clipping.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderOpacity, Node.Render.RenderOpacity.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderOpacity.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderAngle, Node.Render.RenderAngle.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderAngle.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformScale, Node.Render.RenderTransformScale.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTransformScale.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTransformPivot, Node.Render.RenderTransformPivot.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTransformPivot.GetValue())),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::RenderTranslation, Node.Render.RenderTranslation.IsSet(), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTranslation.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetWidgetStateTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ActiveWidgetIndex, Node.WidgetState.ActiveWidgetIndex.IsSet(), FString::FromInt(Node.WidgetState.ActiveWidgetIndex.GetValue())),
		};
		return { Descriptors, UE_ARRAY_COUNT(Descriptors) };
	}

	FTypedWidgetDescriptorRange GetScrollBoxTypedWidgetDescriptors()
	{
		static const FTypedWidgetStringDescriptor Descriptors[] = {
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::Orientation, Node.ScrollBox.Orientation.IsSet(), Node.ScrollBox.Orientation.GetValue()),
			DESIGN2UMG_TYPED_WIDGET_DESCRIPTOR(UMGPropertyKeys::ScrollBarVisibility, Node.ScrollBox.ScrollBarVisibility.IsSet(), Node.ScrollBox.ScrollBarVisibility.GetValue()),
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
