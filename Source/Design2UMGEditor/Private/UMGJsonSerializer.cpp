// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonSerializer.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGValueParsing.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	constexpr TCHAR JsonTypeKey[] = TEXT("Type");
	constexpr TCHAR JsonModeKey[] = TEXT("Mode");
	constexpr TCHAR JsonTransformKey[] = TEXT("Transform");
	constexpr TCHAR JsonRotationKey[] = TEXT("Rotation");
	constexpr TCHAR JsonCenterKey[] = TEXT("Center");
	constexpr TCHAR JsonSizeKey[] = TEXT("Size");
	constexpr TCHAR JsonPositionKey[] = TEXT("Position");
	constexpr TCHAR JsonOffsetKey[] = TEXT("Offset");
	constexpr TCHAR JsonBlurKey[] = TEXT("Blur");
	constexpr TCHAR JsonSpreadKey[] = TEXT("Spread");
	constexpr TCHAR JsonBoxShadowsKey[] = TEXT("BoxShadows");
}

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
		J->SetStringField(TEXT("Padding"), FString::Printf(
			TEXT("%.2f,%.2f,%.2f,%.2f"),
			Slot.Padding.Left, Slot.Padding.Top, Slot.Padding.Right, Slot.Padding.Bottom));
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
	J->SetStringField(JsonTypeKey, Node.Type);
	if (!Node.Id.IsEmpty())
		J->SetStringField(TEXT("Id"), Node.Id);
	J->SetStringField(TEXT("Name"), Node.Name);

	const bool bHasBounds =
		!FMath::IsNearlyZero(Node.Bounds.X) || !FMath::IsNearlyZero(Node.Bounds.Y) ||
		!FMath::IsNearlyZero(Node.Bounds.W) || !FMath::IsNearlyZero(Node.Bounds.H);
	if (bHasBounds)
		J->SetObjectField(TEXT("Bounds"), SerializeBounds(Node.Bounds));

	J->SetObjectField(TEXT("Slot"), SerializeSlot(Node.Slot));

	// TextStyle (v2.4)
	if (Node.TextStyle.FontSize > 0 || Node.TextStyle.SourceFontSize > 0 || Node.TextStyle.LetterSpacing.IsSet())
	{
		TSharedRef<FJsonObject> TS = MakeShared<FJsonObject>();
		if (Node.TextStyle.FontSize > 0)
			TS->SetNumberField(UMGPropertyKeys::FontSize, Node.TextStyle.FontSize);
		if (Node.TextStyle.SourceFontSize > 0)
			TS->SetNumberField(TEXT("SourceFontSize"), Node.TextStyle.SourceFontSize);
		if (Node.TextStyle.LetterSpacing.IsSet())
			TS->SetNumberField(UMGPropertyKeys::LetterSpacing, Node.TextStyle.LetterSpacing.GetValue());
		J->SetObjectField(TEXT("TextStyle"), TS);
	}

	if (Node.WidgetStyle.HasAny())
	{
		const FUMGWidgetStyleData& Style = Node.WidgetStyle;
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
			StyleObject->SetStringField(TEXT("StrokeMargin"), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.StrokeMargin.GetValue()));
		if (Style.ImageMode.IsSet())
			StyleObject->SetStringField(UMGPropertyKeys::ImageMode, Style.ImageMode.GetValue());
		if (Style.ImageTransform.IsSet())
			StyleObject->SetStringField(UMGPropertyKeys::ImageTransform, Style.ImageTransform.GetValue());
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
		J->SetObjectField(TEXT("WidgetStyle"), StyleObject);
	}

	if (Node.Text.HasAny())
	{
		const FUMGTextData& Text = Node.Text;
		TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
		if (Text.Text.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::Text, Text.Text.GetValue());
		if (Text.RichTextStyleRowsJson.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::RichTextStyleRows, Text.RichTextStyleRowsJson.GetValue());
		if (Text.HintText.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::HintText, Text.HintText.GetValue());
		if (Text.FontFamily.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::FontFamily, Text.FontFamily.GetValue());
		if (Text.TypefaceFontName.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::TypefaceFontName, Text.TypefaceFontName.GetValue());
		if (Text.FontStyle.IsSet())
			TextObject->SetStringField(TEXT("FontStyle"), Text.FontStyle.GetValue());
		if (Text.Justification.IsSet())
			TextObject->SetStringField(UMGPropertyKeys::Justification, Text.Justification.GetValue());
		if (Text.bAutoWrapText.IsSet())
			TextObject->SetBoolField(UMGPropertyKeys::AutoWrapText, Text.bAutoWrapText.GetValue());
		if (Text.WrapTextAt.IsSet())
			TextObject->SetNumberField(UMGPropertyKeys::WrapTextAt, Text.WrapTextAt.GetValue());
		if (Text.LineHeightPercentage.IsSet())
			TextObject->SetNumberField(UMGPropertyKeys::LineHeightPercentage, Text.LineHeightPercentage.GetValue());
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
		J->SetObjectField(TEXT("TextData"), TextObject);
	}

	// Brush (v2.4)
	{
		FUMGVisualBrushData BrushData = Node.Brush;
		Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(BrushData);
		const FUMGVisualBrushData& B = BrushData;
		TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
		bool bHasAny = false;
		if (B.OutlineCornerRadius >= 0)    { BJ->SetNumberField(UMGPropertyKeys::OutlineCornerRadius, B.OutlineCornerRadius); bHasAny = true; }
		if (B.HasCornerRadii())            { BJ->SetStringField(UMGPropertyKeys::CornerRadii, FString::Printf(TEXT("%.2f,%.2f,%.2f,%.2f"), B.CornerRadii.X, B.CornerRadii.Y, B.CornerRadii.Z, B.CornerRadii.W)); bHasAny = true; }
		if (B.OutlineThickness.IsSet())    { BJ->SetNumberField(UMGPropertyKeys::OutlineThickness, B.OutlineThickness.GetValue()); bHasAny = true; }
		if (B.DrawAs.IsSet())              { BJ->SetStringField(UMGPropertyKeys::DrawAs, Design2UMGWidgetNodeAccess::SerializeDrawAs(B.DrawAs.GetValue())); bHasAny = true; }
		if (B.BrushColor.IsSet())          { BJ->SetStringField(UMGPropertyKeys::BrushColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.BrushColor.GetValue())); bHasAny = true; }
		if (B.OutlineColor.IsSet())        { BJ->SetStringField(UMGPropertyKeys::OutlineColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.OutlineColor.GetValue())); bHasAny = true; }
		if (B.MaterialRef.IsSet())         { BJ->SetStringField(UMGPropertyKeys::MaterialRef, B.MaterialRef.GetValue()); bHasAny = true; }
		if (B.MaterialParams.IsSet())      { BJ->SetStringField(UMGPropertyKeys::MaterialParams, B.MaterialParams.GetValue()); bHasAny = true; }
		if (B.Image.HasAny())
		{
			TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
			if (B.Image.TexturePath.IsSet())
				ImageObject->SetStringField(UMGPropertyKeys::TexturePath, B.Image.TexturePath.GetValue());
			if (B.Image.Mode.IsSet())
				ImageObject->SetStringField(JsonModeKey, B.Image.Mode.GetValue());
			if (B.Image.Transform.IsSet())
				ImageObject->SetStringField(JsonTransformKey, Design2UMGWidgetNodeAccess::SerializeImageTransform(B.Image.Transform.GetValue()));
			BJ->SetObjectField(TEXT("Image"), ImageObject);
			bHasAny = true;
		}
		if (B.Gradient.HasAny())
		{
			TSharedRef<FJsonObject> GradientObject = MakeShared<FJsonObject>();
			if (B.Gradient.Type.IsSet())
				GradientObject->SetStringField(JsonTypeKey, B.Gradient.Type.GetValue());
			if (B.Gradient.Rotation.IsSet())
				GradientObject->SetNumberField(JsonRotationKey, B.Gradient.Rotation.GetValue());
			if (B.Gradient.Center.IsSet())
				GradientObject->SetStringField(JsonCenterKey, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Center.GetValue()));
			if (B.Gradient.Size.IsSet())
				GradientObject->SetStringField(JsonSizeKey, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Size.GetValue()));
			if (B.Gradient.Stops.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> StopArray;
				StopArray.Reserve(B.Gradient.Stops.Num());
				for (const FUMGVisualBrushData::FGradientStopData& Stop : B.Gradient.Stops)
				{
					TSharedRef<FJsonObject> StopObject = MakeShared<FJsonObject>();
					StopObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Stop.Color));
					StopObject->SetNumberField(JsonPositionKey, Stop.Position);
					StopArray.Add(MakeShared<FJsonValueObject>(StopObject));
				}
				GradientObject->SetArrayField(TEXT("Stops"), StopArray);
			}
			BJ->SetObjectField(TEXT("Gradient"), GradientObject);
			bHasAny = true;
		}
		if (bHasAny)
			J->SetObjectField(TEXT("Brush"), BJ);
	}

	// Effect (v2.5)
	if (Node.Effect.HasAny())
	{
		const FUMGEffectData& E = Node.Effect;
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
			const FVector2D& EffectBlurUVStep = E.EffectBlurUVStep.GetValue();
			EJ->SetStringField(
				UMGPropertyKeys::EffectBlurUVStep,
				FString::Printf(TEXT("%.6f,%.6f"), EffectBlurUVStep.X, EffectBlurUVStep.Y));
		}
		if (E.EffectColor.IsSet())
			EJ->SetStringField(UMGPropertyKeys::EffectColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(E.EffectColor.GetValue()));
		if (E.EffectOffset.IsSet())
		{
			const FVector2D& EffectOffset = E.EffectOffset.GetValue();
			EJ->SetStringField(
				UMGPropertyKeys::EffectOffset,
				FString::Printf(TEXT("%.6f,%.6f"), EffectOffset.X, EffectOffset.Y));
		}
		if (E.RenderingPhase.IsSet())
		{
			const FUMGEffectData::FRenderingPhaseData& RenderingPhase = E.RenderingPhase.GetValue();
			EJ->SetNumberField(UMGPropertyKeys::Phase, RenderingPhase.Phase);
			EJ->SetNumberField(UMGPropertyKeys::PhaseCount, RenderingPhase.PhaseCount);
		}
		J->SetObjectField(TEXT("Effect"), EJ);
	}

	if (Node.Path.HasAny())
	{
		const FUMGPathData& Path = Node.Path;
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
		J->SetObjectField(TEXT("PathData"), PathObject);
	}

	if (Node.Shadow.HasAny())
	{
		const FUMGShadowData& Shadow = Node.Shadow;
		TSharedRef<FJsonObject> ShadowObject = MakeShared<FJsonObject>();
		if (Shadow.ShadowOffset.IsSet())
			ShadowObject->SetStringField(UMGPropertyKeys::ShadowOffset, FString::Printf(TEXT("%.1f,%.1f"), Shadow.ShadowOffset.GetValue().X, Shadow.ShadowOffset.GetValue().Y));
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
				LayerObject->SetStringField(JsonOffsetKey, FString::Printf(TEXT("%.6f,%.6f"), Layer.Offset.X, Layer.Offset.Y));
				LayerObject->SetNumberField(JsonBlurKey, Layer.Blur);
				LayerObject->SetNumberField(JsonSpreadKey, Layer.Spread);
				LayerObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Layer.Color));
				BoxShadowArray.Add(MakeShared<FJsonValueObject>(LayerObject));
			}
			ShadowObject->SetArrayField(JsonBoxShadowsKey, BoxShadowArray);
		}
		J->SetObjectField(TEXT("ShadowData"), ShadowObject);
	}

	// Render (v2.7)
	if (Node.Render.HasAny())
	{
		const FUMGRenderStyleData& Render = Node.Render;
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
			const FVector2D& RenderTransformPivot = Render.RenderTransformPivot.GetValue();
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTransformPivot,
				FString::Printf(TEXT("%.6f,%.6f"), RenderTransformPivot.X, RenderTransformPivot.Y));
		}
		if (Render.RenderTransformScale.IsSet())
		{
			const FVector2D& RenderTransformScale = Render.RenderTransformScale.GetValue();
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTransformScale,
				FString::Printf(TEXT("%.6f,%.6f"), RenderTransformScale.X, RenderTransformScale.Y));
		}
		if (Render.RenderTranslation.IsSet())
		{
			const FVector2D& RenderTranslation = Render.RenderTranslation.GetValue();
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTranslation,
				FString::Printf(TEXT("%.6f,%.6f"), RenderTranslation.X, RenderTranslation.Y));
		}
		J->SetObjectField(TEXT("Render"), RenderObject);
	}

	// WidgetState (v2.6)
	if (Node.WidgetState.HasAny())
	{
		const FUMGWidgetStateData& State = Node.WidgetState;
		TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
		if (State.ActiveWidgetIndex.IsSet())
		{
			StateObject->SetNumberField(UMGPropertyKeys::ActiveWidgetIndex, State.ActiveWidgetIndex.GetValue());
		}
		J->SetObjectField(TEXT("WidgetState"), StateObject);
	}

	// ScrollBox (v2.6)
	if (Node.ScrollBox.HasAny())
	{
		const FUMGScrollBoxData& ScrollBox = Node.ScrollBox;
		TSharedRef<FJsonObject> ScrollBoxObject = MakeShared<FJsonObject>();
		if (ScrollBox.Orientation.IsSet())
		{
			ScrollBoxObject->SetStringField(UMGPropertyKeys::Orientation, ScrollBox.Orientation.GetValue());
		}
		if (ScrollBox.ScrollBarVisibility.IsSet())
		{
			ScrollBoxObject->SetStringField(UMGPropertyKeys::ScrollBarVisibility, ScrollBox.ScrollBarVisibility.GetValue());
		}
		J->SetObjectField(TEXT("ScrollBox"), ScrollBoxObject);
	}

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

