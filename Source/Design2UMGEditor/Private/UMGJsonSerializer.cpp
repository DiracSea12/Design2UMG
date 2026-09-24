// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonSerializer.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGJsonKeys.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGValueParsing.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeAnchor(const FUMGAnchorData& Anchor)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), Anchor.X);
	J->SetNumberField(TEXT("Y"), Anchor.Y);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeAnchors(const FUMGAnchorsData& Anchors)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Min"), SerializeAnchor(Anchors.Min));
	J->SetObjectField(TEXT("Max"), SerializeAnchor(Anchors.Max));
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeOffsets(const FUMGOffsetsData& Offsets)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("Left"), Offsets.Left);
	J->SetNumberField(TEXT("Top"), Offsets.Top);
	J->SetNumberField(TEXT("Right"), Offsets.Right);
	J->SetNumberField(TEXT("Bottom"), Offsets.Bottom);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeSlot(const FUMGSlotData& Slot)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Anchors"), SerializeAnchors(Slot.Anchors));
	J->SetObjectField(TEXT("Alignment"), SerializeAnchor(Slot.Alignment));
	J->SetObjectField(TEXT("Offsets"), SerializeOffsets(Slot.Offsets));

	const bool bHasPadding =
		!FMath::IsNearlyEqual(Slot.Padding.Left, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Top, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Right, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Bottom, 0.0f);
	if (bHasPadding)
	{
		J->SetStringField(UMGPropertyKeys::Padding, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Slot.Padding, 2));
	}

	if (Slot.HorizontalAlignment != EUMGSlotHorizontalAlignment::Fill)
		J->SetStringField(TEXT("HorizontalAlignment"), FUMGValueParser::FormatSlotHorizontalAlignment(Slot.HorizontalAlignment));
	if (Slot.VerticalAlignment != EUMGSlotVerticalAlignment::Fill)
		J->SetStringField(TEXT("VerticalAlignment"), FUMGValueParser::FormatSlotVerticalAlignment(Slot.VerticalAlignment));
	if (Slot.SizeRule != EUMGSlotSizeRule::Auto)
		J->SetStringField(TEXT("SizeRule"), FUMGValueParser::FormatSlotSizeRule(Slot.SizeRule));
	if (!FMath::IsNearlyEqual(Slot.FillWeight, 1.0f))
		J->SetNumberField(TEXT("FillWeight"), Slot.FillWeight);
	if (Slot.Row != 0)
		J->SetNumberField(TEXT("Row"), Slot.Row);
	if (Slot.Column != 0)
		J->SetNumberField(TEXT("Column"), Slot.Column);
	if (Slot.RowSpan != 1)
		J->SetNumberField(TEXT("RowSpan"), Slot.RowSpan);
	if (Slot.ColumnSpan != 1)
		J->SetNumberField(TEXT("ColumnSpan"), Slot.ColumnSpan);
	if (Slot.bAutoSize)
		J->SetBoolField(TEXT("bAutoSize"), true);

	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeBounds(const FDesignPixelBounds& Bounds)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), Bounds.X);
	J->SetNumberField(TEXT("Y"), Bounds.Y);
	J->SetNumberField(TEXT("W"), Bounds.W);
	J->SetNumberField(TEXT("H"), Bounds.H);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeWidgetNode(const FUMGWidgetNode& Node)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(UMGJsonKeys::Type, Node.Type);
	if (!Node.Id.IsEmpty())
		J->SetStringField(TEXT("Id"), Node.Id);
	J->SetStringField(TEXT("Name"), Node.Name);
	if (Node.bSyntheticJustifySpacer)
		J->SetBoolField(TEXT("SyntheticJustifySpacer"), true);

	const bool bHasBounds =
		!FMath::IsNearlyZero(Node.Bounds.X) || !FMath::IsNearlyZero(Node.Bounds.Y) ||
		!FMath::IsNearlyZero(Node.Bounds.W) || !FMath::IsNearlyZero(Node.Bounds.H);
	if (bHasBounds)
		J->SetObjectField(TEXT("Bounds"), SerializeBounds(Node.Bounds));

	J->SetObjectField(TEXT("Slot"), SerializeSlot(Node.Slot));

	// Typed payload sections; a null payload object means the section is omitted entirely.
	if (TSharedPtr<FJsonObject> TextStyleObject = SerializeTextStylePayload(Node.TextStyle))
		J->SetObjectField(TEXT("TextStyle"), TextStyleObject);
	if (TSharedPtr<FJsonObject> WidgetStyleObject = SerializeWidgetStylePayload(Node.WidgetStyle))
		J->SetObjectField(TEXT("WidgetStyle"), WidgetStyleObject);
	if (TSharedPtr<FJsonObject> TextObject = SerializeTextPayload(Node.Text))
		J->SetObjectField(TEXT("TextData"), TextObject);
	if (TSharedPtr<FJsonObject> BrushObject = SerializeBrushPayload(Node.Brush))
		J->SetObjectField(TEXT("Brush"), BrushObject);
	if (TSharedPtr<FJsonObject> EffectObject = SerializeEffectPayload(Node.Effect))
		J->SetObjectField(TEXT("Effect"), EffectObject);
	if (TSharedPtr<FJsonObject> PathObject = SerializePathPayload(Node.Path))
		J->SetObjectField(TEXT("PathData"), PathObject);
	if (TSharedPtr<FJsonObject> ShadowObject = SerializeShadowPayload(Node.Shadow))
		J->SetObjectField(TEXT("ShadowData"), ShadowObject);
	if (TSharedPtr<FJsonObject> RenderObject = SerializeRenderPayload(Node.Render))
		J->SetObjectField(TEXT("Render"), RenderObject);
	if (TSharedPtr<FJsonObject> WidgetStateObject = SerializeWidgetStatePayload(Node.WidgetState))
		J->SetObjectField(TEXT("WidgetState"), WidgetStateObject);
	if (TSharedPtr<FJsonObject> ScrollBoxObject = SerializeScrollBoxPayload(Node.ScrollBox))
		J->SetObjectField(TEXT("ScrollBox"), ScrollBoxObject);

	if (Node.Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Node.Children.Num());
		for (const FUMGWidgetNode& Child : Node.Children)
			Arr.Add(MakeShared<FJsonValueObject>(SerializeWidgetNode(Child)));
		J->SetArrayField(TEXT("Children"), Arr);
	}

	return J;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeTextStylePayload(const FUMGTextStyleData& TextStyle)
{
	// TextStyle (v2.4)
	if (TextStyle.FontSize > 0 || TextStyle.SourceFontSize > 0 || TextStyle.LetterSpacing.IsSet())
	{
		TSharedRef<FJsonObject> TS = MakeShared<FJsonObject>();
		if (TextStyle.FontSize > 0)
			TS->SetNumberField(UMGPropertyKeys::FontSize, TextStyle.FontSize);
		if (TextStyle.SourceFontSize > 0)
			TS->SetNumberField(UMGJsonKeys::SourceFontSize, TextStyle.SourceFontSize);
		if (TextStyle.LetterSpacing.IsSet())
			TS->SetNumberField(UMGPropertyKeys::LetterSpacing, TextStyle.LetterSpacing.GetValue());
		return TS;
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeWidgetStylePayload(const FUMGWidgetStyleData& Style)
{
	if (!Style.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> StyleObject = MakeShared<FJsonObject>();
	if (Style.WidthOverride.IsSet())
		StyleObject->SetNumberField(UMGPropertyKeys::WidthOverride, Style.WidthOverride.GetValue());
	if (Style.HeightOverride.IsSet())
		StyleObject->SetNumberField(UMGPropertyKeys::HeightOverride, Style.HeightOverride.GetValue());
	if (Style.MinDesiredWidth.IsSet())
		StyleObject->SetNumberField(UMGPropertyKeys::MinDesiredWidth, Style.MinDesiredWidth.GetValue());
	if (Style.MinDesiredHeight.IsSet())
		StyleObject->SetNumberField(UMGPropertyKeys::MinDesiredHeight, Style.MinDesiredHeight.GetValue());
	if (Style.Padding.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::Padding, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.Padding.GetValue()));
	if (Style.InnerSlotPadding.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::InnerSlotPadding, Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Style.InnerSlotPadding.GetValue()));
	if (Style.SlotPadding.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::SlotPadding, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.SlotPadding.GetValue()));
	if (Style.StrokePadding.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::StrokePadding, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.StrokePadding.GetValue()));
	if (Style.StrokeMargin.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::StrokeMargin, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.StrokeMargin.GetValue()));
	if (Style.Stretch.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::Stretch, Style.Stretch.GetValue());
	if (Style.StretchDirection.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::StretchDirection, Style.StretchDirection.GetValue());
	if (Style.BackgroundColor.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::BackgroundColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.BackgroundColor.GetValue()));
	if (Style.HoveredColor.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::HoveredColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.HoveredColor.GetValue()));
	if (Style.PressedColor.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::PressedColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.PressedColor.GetValue()));
	if (Style.NamedSlot.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::NamedSlot, Style.NamedSlot.GetValue());
	if (Style.SlotName.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::SlotName, Style.SlotName.GetValue());
	if (Style.Visibility.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::Visibility, Style.Visibility.GetValue());
	if (Style.ToolTipText.IsSet())
		StyleObject->SetStringField(UMGPropertyKeys::ToolTipText, Style.ToolTipText.GetValue());
	if (Style.IsEnabled.IsSet())
		StyleObject->SetBoolField(UMGPropertyKeys::IsEnabled, Style.IsEnabled.GetValue());
	return StyleObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeTextPayload(const FUMGTextData& Text)
{
	if (!Text.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
	if (Text.Text.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::Text, Text.Text.GetValue());
	if (Text.RichTextStyleRows.IsSet())
	{
		TSharedRef<FJsonObject> RowsObject = MakeShared<FJsonObject>();
		for (const auto& Pair : Text.RichTextStyleRows.GetValue())
		{
			const FUMGRichTextStyleRowData& Row = Pair.Value;
			TSharedRef<FJsonObject> RowObject = MakeShared<FJsonObject>();
			if (!Row.FontFamily.IsEmpty()) RowObject->SetStringField(UMGPropertyKeys::FontFamily, Row.FontFamily);
			if (!Row.TypefaceFontName.IsEmpty()) RowObject->SetStringField(UMGPropertyKeys::TypefaceFontName, Row.TypefaceFontName);
			if (Row.FontSize.IsSet()) RowObject->SetNumberField(UMGPropertyKeys::FontSize, Row.FontSize.GetValue());
			if (Row.LetterSpacing.IsSet()) RowObject->SetNumberField(UMGPropertyKeys::LetterSpacing, Row.LetterSpacing.GetValue());
			if (Row.Color.IsSet()) RowObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Row.Color.GetValue()));
			if (Row.bUnderline) RowObject->SetBoolField(UMGPropertyKeys::Underline, true);
			if (Row.bStrikethrough) RowObject->SetBoolField(UMGPropertyKeys::Strikethrough, true);
			if (!Row.Href.IsEmpty()) RowObject->SetStringField(UMGPropertyKeys::Href, Row.Href);
			RowsObject->SetObjectField(Pair.Key, RowObject);
		}
		FString RowsJson;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RowsJson);
		if (FJsonSerializer::Serialize(RowsObject, Writer)) TextObject->SetStringField(UMGPropertyKeys::RichTextStyleRows, RowsJson);
	}
	if (Text.HintText.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::HintText, Text.HintText.GetValue());
	if (Text.FontFamily.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::FontFamily, Text.FontFamily.GetValue());
	if (Text.TypefaceFontName.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::TypefaceFontName, Text.TypefaceFontName.GetValue());
	if (Text.FontStyle.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::FontStyle, Text.FontStyle.GetValue());
	if (Text.Justification.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::Justification, Text.Justification.GetValue());
	if (Text.bAutoWrapText.IsSet())
		TextObject->SetBoolField(UMGPropertyKeys::AutoWrapText, Text.bAutoWrapText.GetValue());
	if (Text.WrapTextAt.IsSet())
		TextObject->SetNumberField(UMGPropertyKeys::WrapTextAt, Text.WrapTextAt.GetValue());
	if (Text.LineHeightPercentage.IsSet())
		TextObject->SetNumberField(UMGPropertyKeys::LineHeightPercentage, Text.LineHeightPercentage.GetValue());
	if (Text.LineHeightBaselineShift.IsSet())
		TextObject->SetNumberField(UMGPropertyKeys::LineHeightBaselineShift, Text.LineHeightBaselineShift.GetValue());
	if (Text.bUnderline.IsSet())
		TextObject->SetBoolField(UMGPropertyKeys::Underline, Text.bUnderline.GetValue());
	if (Text.bStrikethrough.IsSet())
		TextObject->SetBoolField(UMGPropertyKeys::Strikethrough, Text.bStrikethrough.GetValue());
	if (Text.Href.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::Href, Text.Href.GetValue());
	if (Text.TextAlignVertical.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::TextAlignVertical, Text.TextAlignVertical.GetValue());
	if (Text.IconName.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::IconName, Text.IconName.GetValue());
	if (Text.Color.IsSet())
		TextObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Text.Color.GetValue()));
	return TextObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeBrushPayload(const FUMGVisualBrushData& Brush)
{
	// Brush (v2.4)
	const FUMGVisualBrushData& B = Brush;
	TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
	bool bHasAny = false;
	if (B.OutlineCornerRadius.IsSet())   { BJ->SetNumberField(UMGPropertyKeys::OutlineCornerRadius, B.OutlineCornerRadius.GetValue()); bHasAny = true; }
	if (B.CornerRadii.IsSet())           { BJ->SetStringField(UMGPropertyKeys::CornerRadii, Design2UMGWidgetNodeAccess::SerializeWidgetVector4(B.CornerRadii.GetValue(), 2)); bHasAny = true; }
	if (B.OutlineThickness.IsSet())    { BJ->SetNumberField(UMGPropertyKeys::OutlineThickness, B.OutlineThickness.GetValue()); bHasAny = true; }
	if (B.Margin.IsSet()) { BJ->SetStringField(UMGPropertyKeys::BrushMargin, Design2UMGWidgetNodeAccess::SerializeWidgetMargin(B.Margin.GetValue(), 9)); bHasAny = true; }
	if (B.DrawAs.IsSet())              { BJ->SetStringField(UMGPropertyKeys::DrawAs, Design2UMGWidgetNodeAccess::SerializeDrawAs(B.DrawAs.GetValue())); bHasAny = true; }
	if (B.BrushColor.IsSet())          { BJ->SetStringField(UMGPropertyKeys::BrushColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.BrushColor.GetValue())); bHasAny = true; }
	if (B.OutlineColor.IsSet())        { BJ->SetStringField(UMGPropertyKeys::OutlineColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.OutlineColor.GetValue())); bHasAny = true; }
	if (B.MaterialRef.IsSet())         { BJ->SetStringField(UMGPropertyKeys::MaterialRef, B.MaterialRef.GetValue()); bHasAny = true; }
	if (B.MaterialParams.IsSet())      { BJ->SetStringField(UMGPropertyKeys::MaterialParams, B.MaterialParams.GetValue()); bHasAny = true; }
	if (B.Shape.IsSet())
	{
		const auto& Shape = B.Shape.GetValue();
		TSharedRef<FJsonObject> ShapeObject = MakeShared<FJsonObject>();
		if (Shape.Color.IsSet()) ShapeObject->SetStringField(TEXT("Color"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Shape.Color.GetValue()));
		if (Shape.Thickness.IsSet()) ShapeObject->SetNumberField(TEXT("Thickness"), Shape.Thickness.GetValue());
		if (Shape.CornerRadius.IsSet()) ShapeObject->SetNumberField(TEXT("CornerRadius"), Shape.CornerRadius.GetValue());
		if (Shape.BlurPadding.IsSet()) ShapeObject->SetNumberField(TEXT("BlurPadding"), Shape.BlurPadding.GetValue());
		if (Shape.StripeColor.IsSet()) ShapeObject->SetStringField(TEXT("StripeColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Shape.StripeColor.GetValue()));
		if (Shape.StripeWidth.IsSet()) ShapeObject->SetNumberField(TEXT("StripeWidth"), Shape.StripeWidth.GetValue());
		if (Shape.StripeGap.IsSet()) ShapeObject->SetNumberField(TEXT("StripeGap"), Shape.StripeGap.GetValue());
		if (Shape.Inset.IsSet()) ShapeObject->SetNumberField(TEXT("Inset"), Shape.Inset.GetValue());
		if (Shape.CornerRadii.IsSet())
		{
			TArray<TSharedPtr<FJsonValue>> Radii;
			for (int32 Index = 0; Index < 4; ++Index) Radii.Add(MakeShared<FJsonValueNumber>(Shape.CornerRadii.GetValue()[Index]));
			ShapeObject->SetArrayField(TEXT("CornerRadii"), Radii);
		}
		BJ->SetObjectField(TEXT("Shape"), ShapeObject);
		bHasAny = true;
	}
	if (B.Image.HasAny())
	{
		TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
		if (B.Image.TexturePath.IsSet())
			ImageObject->SetStringField(UMGPropertyKeys::TexturePath, B.Image.TexturePath.GetValue());
		if (B.Image.Mode.IsSet())
			ImageObject->SetStringField(UMGJsonKeys::Mode, B.Image.Mode.GetValue());
		if (B.Image.Transform.IsSet())
			ImageObject->SetStringField(UMGJsonKeys::Transform, Design2UMGWidgetNodeAccess::SerializeImageTransform(B.Image.Transform.GetValue()));
		BJ->SetObjectField(TEXT("Image"), ImageObject);
		bHasAny = true;
	}
	if (B.Gradient.HasAny())
	{
		TSharedRef<FJsonObject> GradientObject = MakeShared<FJsonObject>();
		if (B.Gradient.Type.IsSet())
			GradientObject->SetStringField(UMGJsonKeys::Type, B.Gradient.Type.GetValue());
		if (B.Gradient.Rotation.IsSet())
			GradientObject->SetNumberField(UMGJsonKeys::Rotation, B.Gradient.Rotation.GetValue());
		if (B.Gradient.StartPoint.IsSet())
			GradientObject->SetStringField(UMGJsonKeys::StartPoint, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.StartPoint.GetValue()));
		if (B.Gradient.EndPoint.IsSet())
			GradientObject->SetStringField(UMGJsonKeys::EndPoint, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.EndPoint.GetValue()));
		if (B.Gradient.Center.IsSet())
			GradientObject->SetStringField(UMGJsonKeys::Center, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Center.GetValue()));
		if (B.Gradient.Size.IsSet())
			GradientObject->SetStringField(UMGJsonKeys::Size, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Size.GetValue()));
		if (B.Gradient.Stops.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> StopArray;
			StopArray.Reserve(B.Gradient.Stops.Num());
			for (const FUMGVisualBrushData::FGradientStopData& Stop : B.Gradient.Stops)
			{
				TSharedRef<FJsonObject> StopObject = MakeShared<FJsonObject>();
				StopObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Stop.Color));
				StopObject->SetNumberField(UMGJsonKeys::Position, Stop.Position);
				StopArray.Add(MakeShared<FJsonValueObject>(StopObject));
			}
			GradientObject->SetArrayField(UMGJsonKeys::Stops, StopArray);
		}
		BJ->SetObjectField(TEXT("Gradient"), GradientObject);
		bHasAny = true;
	}
	if (!bHasAny)
	{
		return nullptr;
	}
	return BJ;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeEffectPayload(const FUMGEffectData& E)
{
	// Effect (v2.5)
	if (!E.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> EJ = MakeShared<FJsonObject>();
	if (E.BlurStrength.IsSet())
		EJ->SetNumberField(UMGPropertyKeys::BlurStrength, E.BlurStrength.GetValue());
	if (E.bRetainRendering.IsSet())
		EJ->SetBoolField(UMGPropertyKeys::RetainRendering, E.bRetainRendering.GetValue());
	if (E.EffectMaterial.IsSet())
		EJ->SetStringField(UMGPropertyKeys::EffectMaterial, E.EffectMaterial.GetValue());
	if (E.TextureParameter.IsSet())
		EJ->SetStringField(UMGPropertyKeys::TextureParameter, E.TextureParameter.GetValue());
	if (E.EffectBlurUVStep.IsSet())
	{
		EJ->SetStringField(
			UMGPropertyKeys::EffectBlurUVStep,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(E.EffectBlurUVStep.GetValue()));
	}
	if (E.EffectColor.IsSet())
		EJ->SetStringField(UMGPropertyKeys::EffectColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(E.EffectColor.GetValue()));
	if (E.EffectOffset.IsSet())
	{
		EJ->SetStringField(
			UMGPropertyKeys::EffectOffset,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(E.EffectOffset.GetValue()));
	}
	if (E.GeometryMaskShape.IsSet())
	{
		EJ->SetStringField(
			UMGPropertyKeys::GeometryMaskShape,
			E.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::Ellipse
				? TEXT("Ellipse")
				: TEXT("RoundedRectangle"));
	}
	if (E.GeometryMaskSize.IsSet())
	{
		EJ->SetStringField(
			UMGPropertyKeys::GeometryMaskSize,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(E.GeometryMaskSize.GetValue()));
	}
	if (E.GeometryMaskCornerRadii.IsSet())
	{
		EJ->SetStringField(
			UMGPropertyKeys::GeometryMaskCornerRadii,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector4(E.GeometryMaskCornerRadii.GetValue(), 6));
	}
	if (E.RenderingPhase.IsSet())
	{
		const FUMGEffectData::FRenderingPhaseData& RenderingPhase = E.RenderingPhase.GetValue();
		EJ->SetNumberField(UMGPropertyKeys::Phase, RenderingPhase.Phase);
		EJ->SetNumberField(UMGPropertyKeys::PhaseCount, RenderingPhase.PhaseCount);
	}
	return EJ;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializePathPayload(const FUMGPathData& Path)
{
	if (!Path.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> PathObject = MakeShared<FJsonObject>();
	if (Path.Geometry.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathGeometry, Path.Geometry.GetValue());
	if (Path.FillRule.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathFillRule, Path.FillRule.GetValue());
	if (Path.FillColor.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathFillColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.FillColor.GetValue()));
	if (Path.StrokeColor.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathStrokeColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.StrokeColor.GetValue()));
	if (Path.StrokeThickness.IsSet())
		PathObject->SetNumberField(UMGPropertyKeys::PathStrokeThickness, Path.StrokeThickness.GetValue());
	if (Path.StrokeCap.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathStrokeCap, Path.StrokeCap.GetValue());
	if (Path.StrokeJoin.IsSet())
		PathObject->SetStringField(UMGPropertyKeys::PathStrokeJoin, Path.StrokeJoin.GetValue());
	return PathObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeShadowPayload(const FUMGShadowData& Shadow)
{
	if (!Shadow.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> ShadowObject = MakeShared<FJsonObject>();
	if (Shadow.ShadowOffset.IsSet())
		ShadowObject->SetStringField(UMGPropertyKeys::ShadowOffset, Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Shadow.ShadowOffset.GetValue()));
	if (Shadow.ShadowColorAndOpacity.IsSet())
		ShadowObject->SetStringField(UMGPropertyKeys::ShadowColorAndOpacity, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Shadow.ShadowColorAndOpacity.GetValue()));
	if (Shadow.ShadowBlur.IsSet())
		ShadowObject->SetNumberField(UMGPropertyKeys::ShadowBlur, Shadow.ShadowBlur.GetValue());
	if (Shadow.BoxShadows.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> BoxShadowArray;
		BoxShadowArray.Reserve(Shadow.BoxShadows.Num());
		for (const FUMGBoxShadowLayerData& Layer : Shadow.BoxShadows)
		{
			TSharedRef<FJsonObject> LayerObject = MakeShared<FJsonObject>();
			LayerObject->SetStringField(UMGJsonKeys::Offset, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Layer.Offset));
			LayerObject->SetNumberField(UMGJsonKeys::Blur, Layer.Blur);
			LayerObject->SetNumberField(UMGJsonKeys::Spread, Layer.Spread);
			LayerObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Layer.Color));
			BoxShadowArray.Add(MakeShared<FJsonValueObject>(LayerObject));
		}
		ShadowObject->SetArrayField(UMGJsonKeys::BoxShadows, BoxShadowArray);
	}
	return ShadowObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeRenderPayload(const FUMGRenderStyleData& Render)
{
	// Render (v2.7)
	if (!Render.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> RenderObject = MakeShared<FJsonObject>();
	if (Render.Clipping.IsSet())
	{
		RenderObject->SetStringField(UMGPropertyKeys::Clipping, Render.Clipping.GetValue());
	}
	if (Render.RenderOpacity.IsSet())
	{
		RenderObject->SetNumberField(UMGPropertyKeys::RenderOpacity, Render.RenderOpacity.GetValue());
	}
	if (Render.RenderAngle.IsSet())
	{
		RenderObject->SetNumberField(UMGPropertyKeys::RenderAngle, Render.RenderAngle.GetValue());
	}
	if (Render.RenderTransformPivot.IsSet())
	{
		RenderObject->SetStringField(
			UMGPropertyKeys::RenderTransformPivot,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Render.RenderTransformPivot.GetValue()));
	}
	if (Render.RenderTransformScale.IsSet())
	{
		RenderObject->SetStringField(
			UMGPropertyKeys::RenderTransformScale,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Render.RenderTransformScale.GetValue()));
	}
	if (Render.RenderTranslation.IsSet())
	{
		RenderObject->SetStringField(
			UMGPropertyKeys::RenderTranslation,
			Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Render.RenderTranslation.GetValue()));
	}
	return RenderObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeWidgetStatePayload(const FUMGWidgetStateData& State)
{
	// WidgetState (v2.6)
	if (!State.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	if (State.ActiveWidgetIndex.IsSet())
	{
		StateObject->SetNumberField(UMGPropertyKeys::ActiveWidgetIndex, State.ActiveWidgetIndex.GetValue());
	}
	return StateObject;
}

TSharedPtr<FJsonObject> FUMGJsonSerializer::SerializeScrollBoxPayload(const FUMGScrollBoxData& ScrollBox)
{
	// ScrollBox (v2.6)
	if (!ScrollBox.HasAny())
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> ScrollBoxObject = MakeShared<FJsonObject>();
	if (ScrollBox.Orientation.IsSet())
	{
		ScrollBoxObject->SetStringField(UMGPropertyKeys::Orientation, ScrollBox.Orientation.GetValue());
	}
	if (ScrollBox.ScrollBarVisibility.IsSet())
	{
		ScrollBoxObject->SetStringField(UMGPropertyKeys::ScrollBarVisibility, ScrollBox.ScrollBarVisibility.GetValue());
	}
	return ScrollBoxObject;
}

FString FUMGJsonSerializer::Serialize(const FUMGWidgetData& Data)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("WidgetName"), Data.WidgetName);
	Root->SetObjectField(TEXT("RootWidget"), SerializeWidgetNode(Data.RootWidget));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
