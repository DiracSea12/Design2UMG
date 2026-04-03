// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonSerializer.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGPropertyKeys.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeBounds(const FBounds& Bounds)
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
	J->SetStringField(TEXT("Type"), Node.Type);
	if (!Node.Id.IsEmpty())
		J->SetStringField(TEXT("Id"), Node.Id);
	J->SetStringField(TEXT("Name"), Node.Name);

	const bool bHasBounds =
		!FMath::IsNearlyZero(Node.Bounds.X) || !FMath::IsNearlyZero(Node.Bounds.Y) ||
		!FMath::IsNearlyZero(Node.Bounds.W) || !FMath::IsNearlyZero(Node.Bounds.H);
	if (bHasBounds)
		J->SetObjectField(TEXT("Bounds"), SerializeBounds(Node.Bounds));

	J->SetObjectField(TEXT("Slot"), SerializeSlot(Node.Slot));

	const FUMGPropertyBag LegacyFallbackProperties = Design2UMGWidgetNodeAccess::BuildLegacyFallbackPropertyBag(Node);
	if (LegacyFallbackProperties.Num() > 0)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		LegacyFallbackProperties.ForEachSerialized([&Props](const FString& Key, const FString& Value)
		{
			Props->SetStringField(Key, Value);
		});
		if (Props->Values.Num() > 0)
		{
			J->SetObjectField(TEXT("Properties"), Props);
		}
	}

	// TextStyle (v2.4)
	if (Node.TextStyle.FontSize > 0 || Node.TextStyle.SourceFontSize > 0 || Node.TextStyle.bHasLetterSpacing)
	{
		TSharedRef<FJsonObject> TS = MakeShared<FJsonObject>();
		if (Node.TextStyle.FontSize > 0)
			TS->SetNumberField(TEXT("FontSize"), Node.TextStyle.FontSize);
		if (Node.TextStyle.SourceFontSize > 0)
			TS->SetNumberField(TEXT("SourceFontSize"), Node.TextStyle.SourceFontSize);
		if (Node.TextStyle.bHasLetterSpacing)
			TS->SetNumberField(TEXT("LetterSpacing"), Node.TextStyle.LetterSpacing);
		J->SetObjectField(TEXT("TextStyle"), TS);
	}

	if (Node.WidgetStyle.HasAny())
	{
		const FUMGWidgetStyleData& Style = Node.WidgetStyle;
		TSharedRef<FJsonObject> StyleObject = MakeShared<FJsonObject>();
		if (Style.bHasWidthOverride)
			StyleObject->SetNumberField(TEXT("WidthOverride"), Style.WidthOverride);
		if (Style.bHasHeightOverride)
			StyleObject->SetNumberField(TEXT("HeightOverride"), Style.HeightOverride);
		if (Style.bHasMinDesiredWidth)
			StyleObject->SetNumberField(TEXT("MinDesiredWidth"), Style.MinDesiredWidth);
		if (Style.bHasMinDesiredHeight)
			StyleObject->SetNumberField(TEXT("MinDesiredHeight"), Style.MinDesiredHeight);
		if (Style.bHasPadding)
			StyleObject->SetStringField(TEXT("Padding"), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.Padding));
		if (Style.bHasInnerSlotPadding)
			StyleObject->SetStringField(TEXT("InnerSlotPadding"), Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(Style.InnerSlotPadding));
		if (Style.bHasSlotPadding)
			StyleObject->SetStringField(TEXT("SlotPadding"), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.SlotPadding));
		if (Style.bHasStrokePadding)
			StyleObject->SetStringField(TEXT("StrokePadding"), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.StrokePadding));
		if (Style.bHasStrokeMargin)
			StyleObject->SetStringField(TEXT("StrokeMargin"), Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Style.StrokeMargin));
		if (Style.bHasImageMode)
			StyleObject->SetStringField(TEXT("ImageMode"), Style.ImageMode);
		if (Style.bHasImageTransform)
			StyleObject->SetStringField(TEXT("ImageTransform"), Style.ImageTransform);
		if (Style.bHasStretch)
			StyleObject->SetStringField(TEXT("Stretch"), Style.Stretch);
		if (Style.bHasStretchDirection)
			StyleObject->SetStringField(TEXT("StretchDirection"), Style.StretchDirection);
		if (Style.bHasBackgroundColor)
			StyleObject->SetStringField(TEXT("BackgroundColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.BackgroundColor));
		if (Style.bHasHoveredColor)
			StyleObject->SetStringField(TEXT("HoveredColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.HoveredColor));
		if (Style.bHasPressedColor)
			StyleObject->SetStringField(TEXT("PressedColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Style.PressedColor));
		if (Style.bHasNamedSlot)
			StyleObject->SetStringField(TEXT("NamedSlot"), Style.NamedSlot);
		if (Style.bHasSlotName)
			StyleObject->SetStringField(TEXT("SlotName"), Style.SlotName);
		if (Style.bHasVisibility)
			StyleObject->SetStringField(TEXT("Visibility"), Style.Visibility);
		if (Style.bHasToolTipText)
			StyleObject->SetStringField(TEXT("ToolTipText"), Style.ToolTipText);
		if (Style.bHasIsEnabled)
			StyleObject->SetBoolField(TEXT("IsEnabled"), Style.bIsEnabled);
		J->SetObjectField(TEXT("WidgetStyle"), StyleObject);
	}

	if (Node.Text.HasAny())
	{
		const FUMGTextData& Text = Node.Text;
		TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
		if (Text.bHasText)
			TextObject->SetStringField(TEXT("Text"), Text.Text);
		if (Text.bHasHintText)
			TextObject->SetStringField(TEXT("HintText"), Text.HintText);
		if (Text.bHasFontFamily)
			TextObject->SetStringField(TEXT("FontFamily"), Text.FontFamily);
		if (Text.bHasTypefaceFontName)
			TextObject->SetStringField(TEXT("TypefaceFontName"), Text.TypefaceFontName);
		if (Text.bHasFontStyle)
			TextObject->SetStringField(TEXT("FontStyle"), Text.FontStyle);
		if (Text.bHasJustification)
			TextObject->SetStringField(TEXT("Justification"), Text.Justification);
		if (Text.bHasAutoWrapText)
			TextObject->SetBoolField(TEXT("AutoWrapText"), Text.bAutoWrapText);
		if (Text.bHasWrapTextAt)
			TextObject->SetNumberField(TEXT("WrapTextAt"), Text.WrapTextAt);
		if (Text.bHasLineHeightPercentage)
			TextObject->SetNumberField(TEXT("LineHeightPercentage"), Text.LineHeightPercentage);
		if (Text.bHasUnderline)
			TextObject->SetBoolField(TEXT("Underline"), Text.bUnderline);
		if (Text.bHasStrikethrough)
			TextObject->SetBoolField(TEXT("Strikethrough"), Text.bStrikethrough);
		if (Text.bHasHref)
			TextObject->SetStringField(TEXT("Href"), Text.Href);
		if (Text.bHasTextAlignVertical)
			TextObject->SetStringField(TEXT("TextAlignVertical"), Text.TextAlignVertical);
		if (Text.bHasIconName)
			TextObject->SetStringField(TEXT("IconName"), Text.IconName);
		if (Text.bHasColor)
			TextObject->SetStringField(TEXT("Color"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Text.Color));
		J->SetObjectField(TEXT("TextData"), TextObject);
	}

	// Brush (v2.4)
	{
		const FUMGVisualBrushData& B = Node.Brush;
		TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
		bool bHasAny = false;
		if (B.OutlineCornerRadius >= 0)    { BJ->SetNumberField(TEXT("OutlineCornerRadius"), B.OutlineCornerRadius); bHasAny = true; }
		if (B.HasCornerRadii())            { BJ->SetStringField(TEXT("CornerRadii"), FString::Printf(TEXT("%.2f,%.2f,%.2f,%.2f"), B.CornerRadii.X, B.CornerRadii.Y, B.CornerRadii.Z, B.CornerRadii.W)); bHasAny = true; }
		if (B.bHasOutlineThickness)        { BJ->SetNumberField(TEXT("OutlineThickness"), B.OutlineThickness); bHasAny = true; }
		if (B.bHasDrawAs)                  { BJ->SetStringField(TEXT("DrawAs"), Design2UMGWidgetNodeAccess::SerializeDrawAs(B.DrawAs)); bHasAny = true; }
		if (B.bHasBrushColor)              { BJ->SetStringField(TEXT("BrushColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.BrushColor)); bHasAny = true; }
		if (B.bHasOutlineColor)            { BJ->SetStringField(TEXT("OutlineColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(B.OutlineColor)); bHasAny = true; }
		if (B.bHasMaterialRef || !B.MaterialRef.IsEmpty())        { BJ->SetStringField(TEXT("MaterialRef"), B.MaterialRef); bHasAny = true; }
		if (B.bHasMaterialParams || !B.MaterialParams.IsEmpty())  { BJ->SetStringField(TEXT("MaterialParams"), B.MaterialParams); bHasAny = true; }
		if (!B.TexturePath.IsEmpty())      { BJ->SetStringField(TEXT("TexturePath"), B.TexturePath); bHasAny = true; }
		if (B.Image.HasAny())
		{
			TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
			if (B.Image.bHasTexturePath)
				ImageObject->SetStringField(TEXT("TexturePath"), B.Image.TexturePath);
			if (B.Image.bHasMode)
				ImageObject->SetStringField(TEXT("Mode"), B.Image.Mode);
			if (B.Image.bHasTransform)
				ImageObject->SetStringField(TEXT("Transform"), Design2UMGWidgetNodeAccess::SerializeImageTransform(B.Image.Transform));
			BJ->SetObjectField(TEXT("Image"), ImageObject);
			bHasAny = true;
		}
		if (B.Gradient.HasAny())
		{
			TSharedRef<FJsonObject> GradientObject = MakeShared<FJsonObject>();
			if (B.Gradient.bHasType)
				GradientObject->SetStringField(TEXT("Type"), B.Gradient.Type);
			if (B.Gradient.bHasRotation)
				GradientObject->SetNumberField(TEXT("Rotation"), B.Gradient.Rotation);
			if (B.Gradient.bHasCenter)
				GradientObject->SetStringField(TEXT("Center"), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Center));
			if (B.Gradient.bHasSize)
				GradientObject->SetStringField(TEXT("Size"), Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(B.Gradient.Size));
			if (B.Gradient.Stops.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> StopArray;
				StopArray.Reserve(B.Gradient.Stops.Num());
				for (const FUMGVisualBrushData::FGradientStopData& Stop : B.Gradient.Stops)
				{
					TSharedRef<FJsonObject> StopObject = MakeShared<FJsonObject>();
					StopObject->SetStringField(TEXT("Color"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Stop.Color));
					StopObject->SetNumberField(TEXT("Position"), Stop.Position);
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
			EJ->SetNumberField(TEXT("BlurStrength"), E.BlurStrength);
		if (E.bHasRetainRendering)
			EJ->SetBoolField(TEXT("RetainRendering"), E.bRetainRendering);
		if (E.bHasEffectMaterial || !E.EffectMaterial.IsEmpty())
			EJ->SetStringField(TEXT("EffectMaterial"), E.EffectMaterial);
		if (E.bHasTextureParameter || !E.TextureParameter.IsEmpty())
			EJ->SetStringField(TEXT("TextureParameter"), E.TextureParameter);
		if (E.bHasEffectBlurUVStep)
		{
			EJ->SetStringField(
				TEXT("EffectBlurUVStep"),
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectBlurUVStep.X, E.EffectBlurUVStep.Y));
		}
		if (E.bHasEffectColor)
			EJ->SetStringField(TEXT("EffectColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(E.EffectColor));
		if (E.bHasEffectOffset)
		{
			EJ->SetStringField(
				TEXT("EffectOffset"),
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectOffset.X, E.EffectOffset.Y));
		}
		if (E.bHasRenderingPhase)
		{
			EJ->SetNumberField(TEXT("Phase"), E.Phase);
			EJ->SetNumberField(TEXT("PhaseCount"), E.PhaseCount);
		}
		J->SetObjectField(TEXT("Effect"), EJ);
	}

	if (Node.Path.HasAny())
	{
		const FUMGPathData& Path = Node.Path;
		TSharedRef<FJsonObject> PathObject = MakeShared<FJsonObject>();
		if (Path.bHasGeometry)
			PathObject->SetStringField(TEXT("Geometry"), Path.Geometry);
		if (Path.bHasFillRule)
			PathObject->SetStringField(TEXT("FillRule"), Path.FillRule);
		if (Path.bHasFillColor)
			PathObject->SetStringField(TEXT("FillColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.FillColor));
		if (Path.bHasStrokeColor)
			PathObject->SetStringField(TEXT("StrokeColor"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Path.StrokeColor));
		if (Path.bHasStrokeThickness)
			PathObject->SetNumberField(TEXT("StrokeThickness"), Path.StrokeThickness);
		if (Path.bHasStrokeCap)
			PathObject->SetStringField(TEXT("StrokeCap"), Path.StrokeCap);
		if (Path.bHasStrokeJoin)
			PathObject->SetStringField(TEXT("StrokeJoin"), Path.StrokeJoin);
		J->SetObjectField(TEXT("PathData"), PathObject);
	}

	if (Node.Shadow.HasAny())
	{
		const FUMGShadowData& Shadow = Node.Shadow;
		TSharedRef<FJsonObject> ShadowObject = MakeShared<FJsonObject>();
		if (Shadow.bHasShadowOffset)
			ShadowObject->SetStringField(TEXT("ShadowOffset"), FString::Printf(TEXT("%.1f,%.1f"), Shadow.ShadowOffset.X, Shadow.ShadowOffset.Y));
		if (Shadow.bHasShadowColorAndOpacity)
			ShadowObject->SetStringField(TEXT("ShadowColorAndOpacity"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Shadow.ShadowColorAndOpacity));
		if (Shadow.bHasShadowBlur)
			ShadowObject->SetNumberField(TEXT("ShadowBlur"), Shadow.ShadowBlur);
		if (Shadow.BoxShadows.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> BoxShadowArray;
			BoxShadowArray.Reserve(Shadow.BoxShadows.Num());
			for (const FUMGBoxShadowLayerData& Layer : Shadow.BoxShadows)
			{
				TSharedRef<FJsonObject> LayerObject = MakeShared<FJsonObject>();
				LayerObject->SetStringField(TEXT("Offset"), FString::Printf(TEXT("%.6f,%.6f"), Layer.Offset.X, Layer.Offset.Y));
				LayerObject->SetNumberField(TEXT("Blur"), Layer.Blur);
				LayerObject->SetNumberField(TEXT("Spread"), Layer.Spread);
				LayerObject->SetStringField(TEXT("Color"), Design2UMGWidgetNodeAccess::SerializeWidgetColor(Layer.Color));
				BoxShadowArray.Add(MakeShared<FJsonValueObject>(LayerObject));
			}
			ShadowObject->SetArrayField(TEXT("BoxShadows"), BoxShadowArray);
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
			RenderObject->SetStringField(TEXT("Clipping"), Render.Clipping);
		}
		if (Render.bHasRenderOpacity)
		{
			RenderObject->SetNumberField(TEXT("RenderOpacity"), Render.RenderOpacity);
		}
		if (Render.bHasRenderAngle)
		{
			RenderObject->SetNumberField(TEXT("RenderAngle"), Render.RenderAngle);
		}
		if (Render.bHasRenderTransformPivot)
		{
			RenderObject->SetStringField(
				TEXT("RenderTransformPivot"),
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderTransformPivot.X, Render.RenderTransformPivot.Y));
		}
		if (Render.bHasRenderScale)
		{
			RenderObject->SetStringField(
				TEXT("RenderScale"),
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderScale.X, Render.RenderScale.Y));
		}
		if (Render.bHasRenderTransformScale)
		{
			RenderObject->SetStringField(
				TEXT("RenderTransformScale"),
				FString::Printf(TEXT("%.6f,%.6f"), Render.RenderTransformScale.X, Render.RenderTransformScale.Y));
		}
		if (Render.bHasRenderTranslation)
		{
			RenderObject->SetStringField(
				TEXT("RenderTranslation"),
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
			StateObject->SetNumberField(TEXT("ActiveWidgetIndex"), State.ActiveWidgetIndex);
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

