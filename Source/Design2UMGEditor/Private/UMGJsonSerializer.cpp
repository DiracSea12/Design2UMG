// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonSerializer.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
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

	if (Slot.HorizontalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("HorizontalAlignment"), Slot.HorizontalAlignment);
	if (Slot.VerticalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("VerticalAlignment"), Slot.VerticalAlignment);
	if (Slot.SizeRule != TEXT("Auto"))
		J->SetStringField(TEXT("SizeRule"), Slot.SizeRule);
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
	if (Node.TextStyle.FontSize > 0 || Node.TextStyle.SourceFontSize > 0 || Node.TextStyle.bHasLetterSpacing)
	{
		TSharedRef<FJsonObject> TS = MakeShared<FJsonObject>();
		if (Node.TextStyle.FontSize > 0)
			TS->SetNumberField(UMGPropertyKeys::FontSize, Node.TextStyle.FontSize);
		if (Node.TextStyle.SourceFontSize > 0)
			TS->SetNumberField(TEXT("SourceFontSize"), Node.TextStyle.SourceFontSize);
		if (Node.TextStyle.bHasLetterSpacing)
			TS->SetNumberField(UMGPropertyKeys::LetterSpacing, Node.TextStyle.LetterSpacing);
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
		if (Text.bHasText)
			TextObject->SetStringField(UMGPropertyKeys::Text, Text.Text);
		if (Text.bHasRichTextStyleRows)
			TextObject->SetStringField(UMGPropertyKeys::RichTextStyleRows, Text.RichTextStyleRowsJson);
		if (Text.bHasHintText)
			TextObject->SetStringField(UMGPropertyKeys::HintText, Text.HintText);
		if (Text.bHasFontFamily)
			TextObject->SetStringField(UMGPropertyKeys::FontFamily, Text.FontFamily);
		if (Text.bHasTypefaceFontName)
			TextObject->SetStringField(UMGPropertyKeys::TypefaceFontName, Text.TypefaceFontName);
		if (Text.bHasFontStyle)
			TextObject->SetStringField(TEXT("FontStyle"), Text.FontStyle);
		if (Text.bHasJustification)
			TextObject->SetStringField(UMGPropertyKeys::Justification, Text.Justification);
		if (Text.bHasAutoWrapText)
			TextObject->SetBoolField(UMGPropertyKeys::AutoWrapText, Text.bAutoWrapText);
		if (Text.bHasWrapTextAt)
			TextObject->SetNumberField(UMGPropertyKeys::WrapTextAt, Text.WrapTextAt);
		if (Text.bHasLineHeightPercentage)
			TextObject->SetNumberField(UMGPropertyKeys::LineHeightPercentage, Text.LineHeightPercentage);
		if (Text.bHasUnderline)
			TextObject->SetBoolField(UMGPropertyKeys::Underline, Text.bUnderline);
		if (Text.bHasStrikethrough)
			TextObject->SetBoolField(UMGPropertyKeys::Strikethrough, Text.bStrikethrough);
		if (Text.bHasHref)
			TextObject->SetStringField(UMGPropertyKeys::Href, Text.Href);
		if (Text.bHasTextAlignVertical)
			TextObject->SetStringField(UMGPropertyKeys::TextAlignVertical, Text.TextAlignVertical);
		if (Text.bHasIconName)
			TextObject->SetStringField(UMGPropertyKeys::IconName, Text.IconName);
		if (Text.bHasColor)
			TextObject->SetStringField(UMGPropertyKeys::Color, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Text.Color));
		J->SetObjectField(TEXT("TextData"), TextObject);
	}

	// Brush (v2.4)
	{
		const FUMGVisualBrushData& B = Node.Brush;
		TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
		bool bHasAny = false;
		if (B.OutlineCornerRadius >= 0)    { BJ->SetNumberField(UMGPropertyKeys::OutlineCornerRadius, B.OutlineCornerRadius); bHasAny = true; }
		if (B.HasCornerRadii())            { BJ->SetStringField(UMGPropertyKeys::CornerRadii, FString::Printf(TEXT("%.2f,%.2f,%.2f,%.2f"), B.CornerRadii.X, B.CornerRadii.Y, B.CornerRadii.Z, B.CornerRadii.W)); bHasAny = true; }
		if (B.bHasOutlineThickness)        { BJ->SetNumberField(UMGPropertyKeys::OutlineThickness, B.OutlineThickness); bHasAny = true; }
		if (B.bHasDrawAs)                  { BJ->SetStringField(UMGPropertyKeys::DrawAs, Design2UMGWidgetNodeAccess::SerializeDrawAs(B.DrawAs)); bHasAny = true; }
		if (B.bHasBrushColor)              { BJ->SetStringField(UMGPropertyKeys::BrushColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.BrushColor)); bHasAny = true; }
		if (B.bHasOutlineColor)            { BJ->SetStringField(UMGPropertyKeys::OutlineColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.OutlineColor)); bHasAny = true; }
		if (B.bHasMaterialRef || !B.MaterialRef.IsEmpty())        { BJ->SetStringField(UMGPropertyKeys::MaterialRef, B.MaterialRef); bHasAny = true; }
		if (B.bHasMaterialParams || !B.MaterialParams.IsEmpty())  { BJ->SetStringField(UMGPropertyKeys::MaterialParams, B.MaterialParams); bHasAny = true; }
		if (!B.TexturePath.IsEmpty())      { BJ->SetStringField(UMGPropertyKeys::TexturePath, B.TexturePath); bHasAny = true; }
		if (B.Image.HasAny())
		{
			TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
			if (B.Image.bHasTexturePath)
				ImageObject->SetStringField(UMGPropertyKeys::TexturePath, B.Image.TexturePath);
			if (B.Image.bHasMode)
				ImageObject->SetStringField(JsonModeKey, B.Image.Mode);
			if (B.Image.bHasTransform)
				ImageObject->SetStringField(JsonTransformKey, Design2UMGWidgetNodeAccess::SerializeImageTransform(B.Image.Transform));
			BJ->SetObjectField(TEXT("Image"), ImageObject);
			bHasAny = true;
		}
		if (B.Gradient.HasAny())
		{
			TSharedRef<FJsonObject> GradientObject = MakeShared<FJsonObject>();
			if (B.Gradient.bHasType)
				GradientObject->SetStringField(JsonTypeKey, B.Gradient.Type);
			if (B.Gradient.bHasRotation)
				GradientObject->SetNumberField(JsonRotationKey, B.Gradient.Rotation);
			if (B.Gradient.bHasCenter)
				GradientObject->SetStringField(JsonCenterKey, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Center));
			if (B.Gradient.bHasSize)
				GradientObject->SetStringField(JsonSizeKey, Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Size));
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
		if (E.bHasBlurStrength)
			EJ->SetNumberField(UMGPropertyKeys::BlurStrength, E.BlurStrength);
		if (E.bHasRetainRendering)
			EJ->SetBoolField(UMGPropertyKeys::RetainRendering, E.bRetainRendering);
		if (E.bHasEffectMaterial || !E.EffectMaterial.IsEmpty())
			EJ->SetStringField(UMGPropertyKeys::EffectMaterial, E.EffectMaterial);
		if (E.bHasTextureParameter || !E.TextureParameter.IsEmpty())
			EJ->SetStringField(UMGPropertyKeys::TextureParameter, E.TextureParameter);
		if (E.bHasEffectBlurUVStep)
		{
			EJ->SetStringField(
				UMGPropertyKeys::EffectBlurUVStep,
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectBlurUVStep.X, E.EffectBlurUVStep.Y));
		}
		if (E.bHasEffectColor)
			EJ->SetStringField(UMGPropertyKeys::EffectColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(E.EffectColor));
		if (E.bHasEffectOffset)
		{
			EJ->SetStringField(
				UMGPropertyKeys::EffectOffset,
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectOffset.X, E.EffectOffset.Y));
		}
		if (E.bHasRenderingPhase)
		{
			EJ->SetNumberField(UMGPropertyKeys::Phase, E.Phase);
			EJ->SetNumberField(UMGPropertyKeys::PhaseCount, E.PhaseCount);
		}
		J->SetObjectField(TEXT("Effect"), EJ);
	}

	if (Node.Path.HasAny())
	{
		const FUMGPathData& Path = Node.Path;
		TSharedRef<FJsonObject> PathObject = MakeShared<FJsonObject>();
		if (Path.bHasGeometry)
			PathObject->SetStringField(UMGPropertyKeys::PathGeometry, Path.Geometry);
		if (Path.bHasFillRule)
			PathObject->SetStringField(UMGPropertyKeys::PathFillRule, Path.FillRule);
		if (Path.bHasFillColor)
			PathObject->SetStringField(UMGPropertyKeys::PathFillColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.FillColor));
		if (Path.bHasStrokeColor)
			PathObject->SetStringField(UMGPropertyKeys::PathStrokeColor, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.StrokeColor));
		if (Path.bHasStrokeThickness)
			PathObject->SetNumberField(UMGPropertyKeys::PathStrokeThickness, Path.StrokeThickness);
		if (Path.bHasStrokeCap)
			PathObject->SetStringField(UMGPropertyKeys::PathStrokeCap, Path.StrokeCap);
		if (Path.bHasStrokeJoin)
			PathObject->SetStringField(UMGPropertyKeys::PathStrokeJoin, Path.StrokeJoin);
		J->SetObjectField(TEXT("PathData"), PathObject);
	}

	if (Node.Shadow.HasAny())
	{
		const FUMGShadowData& Shadow = Node.Shadow;
		TSharedRef<FJsonObject> ShadowObject = MakeShared<FJsonObject>();
		if (Shadow.bHasShadowOffset)
			ShadowObject->SetStringField(UMGPropertyKeys::ShadowOffset, FString::Printf(TEXT("%.1f,%.1f"), Shadow.ShadowOffset.X, Shadow.ShadowOffset.Y));
		if (Shadow.bHasShadowColorAndOpacity)
			ShadowObject->SetStringField(UMGPropertyKeys::ShadowColorAndOpacity, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Shadow.ShadowColorAndOpacity));
		if (Shadow.bHasShadowBlur)
			ShadowObject->SetNumberField(UMGPropertyKeys::ShadowBlur, Shadow.ShadowBlur);
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
		if (Render.bHasClipping && !Render.Clipping.IsEmpty())
		{
			RenderObject->SetStringField(UMGPropertyKeys::Clipping, Render.Clipping);
		}
		if (Render.bHasRenderOpacity)
		{
			RenderObject->SetNumberField(UMGPropertyKeys::RenderOpacity, Render.RenderOpacity);
		}
		if (Render.bHasRenderAngle)
		{
			RenderObject->SetNumberField(UMGPropertyKeys::RenderAngle, Render.RenderAngle);
		}
		if (Render.bHasRenderTransformPivot)
		{
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTransformPivot,
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderTransformPivot.X, Render.RenderTransformPivot.Y));
		}
		if (Render.bHasRenderTransformScale)
		{
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTransformScale,
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderTransformScale.X, Render.RenderTransformScale.Y));
		}
		if (Render.bHasRenderTranslation)
		{
			RenderObject->SetStringField(
				UMGPropertyKeys::RenderTranslation,
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderTranslation.X, Render.RenderTranslation.Y));
		}
		J->SetObjectField(TEXT("Render"), RenderObject);
	}

	// WidgetState (v2.6)
	if (Node.WidgetState.HasAny())
	{
		const FUMGWidgetStateData& State = Node.WidgetState;
		TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
		if (State.bHasActiveWidgetIndex)
		{
			StateObject->SetNumberField(UMGPropertyKeys::ActiveWidgetIndex, State.ActiveWidgetIndex);
		}
		J->SetObjectField(TEXT("WidgetState"), StateObject);
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

