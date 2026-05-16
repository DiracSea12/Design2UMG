// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParsing.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

// ============================================================================
// Helper functions.
// ============================================================================

namespace
{
	constexpr TCHAR JsonTypeKey[] = TEXT("Type");
	constexpr TCHAR JsonModeKey[] = TEXT("Mode");
	constexpr TCHAR JsonTransformKey[] = TEXT("Transform");
	constexpr TCHAR JsonRotationKey[] = TEXT("Rotation");
	constexpr TCHAR JsonCenterKey[] = TEXT("Center");
	constexpr TCHAR JsonSizeKey[] = TEXT("Size");
	constexpr TCHAR JsonStopsKey[] = TEXT("Stops");
	constexpr TCHAR JsonPositionKey[] = TEXT("Position");
	constexpr TCHAR JsonBoxShadowsKey[] = TEXT("BoxShadows");
	constexpr TCHAR JsonOffsetKey[] = TEXT("Offset");
	constexpr TCHAR JsonBlurKey[] = TEXT("Blur");
	constexpr TCHAR JsonSpreadKey[] = TEXT("Spread");

	// Parse Padding values, supporting a single number or an "L,T,R,B" string.
	FMargin ParsePaddingValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (Value->Type == EJson::Number)
		{
			return FMargin(Value->AsNumber());
		}

		if (Value->Type == EJson::String)
		{
			TArray<FString> Parts;
			Value->AsString().ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				return FMargin(
					FCString::Atof(*Parts[0].TrimStartAndEnd()),
					FCString::Atof(*Parts[1].TrimStartAndEnd()),
					FCString::Atof(*Parts[2].TrimStartAndEnd()),
					FCString::Atof(*Parts[3].TrimStartAndEnd())
				);
			}
			if (Parts.Num() == 1)
			{
				return FMargin(FCString::Atof(*Parts[0].TrimStartAndEnd()));
			}
		}

		return FMargin(0);
	}

}

// ============================================================================
// Main parsing logic.
// ============================================================================

bool FUMGJsonParser::ParseJson(const FString& JsonString, FUMGWidgetData& OutWidgetData, FString* OutError)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		if (OutError) *OutError = TEXT("Invalid JSON");
		return false;
	}

	// Parse RootWidget.
	const TSharedPtr<FJsonObject>* RootWidget;
	if (!Root->TryGetObjectField(TEXT("RootWidget"), RootWidget) || !ParseWidgetNode(*RootWidget, OutWidgetData.RootWidget))
	{
		if (OutError) *OutError = TEXT("Invalid UI structure");
		return false;
	}

	// Treat an empty pure-layout container as an invalid result.
	// Interactive widgets such as CheckBox can also derive from panel types, so keep this allowlist narrow.
	static const TSet<FString> ContainerTypes = {
		TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"),
		TEXT("Overlay"), TEXT("ScrollBox"), TEXT("GridPanel"),
		TEXT("UniformGridPanel"), TEXT("WrapBox"), TEXT("WidgetSwitcher"),
		TEXT("SafeZone")
	};
	if (OutWidgetData.RootWidget.Children.Num() == 0 &&
		ContainerTypes.Contains(OutWidgetData.RootWidget.Type))
	{
		if (OutError) *OutError = TEXT("Empty UI result");
		return false;
	}

	Root->TryGetStringField(TEXT("WidgetName"), OutWidgetData.WidgetName);
	if (OutWidgetData.WidgetName.IsEmpty()) OutWidgetData.WidgetName = TEXT("WBP_Generated");

	return true;
}

bool FUMGJsonParser::ParseWidgetNode(const TSharedPtr<FJsonObject>& JsonObject, FUMGWidgetNode& OutNode)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	// Parse required fields without crashing on missing keys.
	if (!JsonObject->TryGetStringField(JsonTypeKey, OutNode.Type) || OutNode.Type.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FUMGJsonParser: Widget node missing Type field"));
		return false;
	}

	// Name is optional; synthesize one when absent.
	if (!JsonObject->TryGetStringField(TEXT("Name"), OutNode.Name) || OutNode.Name.IsEmpty())
	{
		OutNode.Name = FString::Printf(TEXT("%s_%s"), *OutNode.Type, *FGuid::NewGuid().ToString(EGuidFormats::Short));
	}

	// Parse v2.2 path-based stable id.
	JsonObject->TryGetStringField(TEXT("Id"), OutNode.Id);

	// Parse Bounds, added in v2.2 for normalized coordinates.
	const TSharedPtr<FJsonObject>* BoundsObject;
	if (JsonObject->TryGetObjectField(TEXT("Bounds"), BoundsObject))
	{
		double Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("X"), Val)) OutNode.Bounds.X = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("Y"), Val)) OutNode.Bounds.Y = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("W"), Val)) OutNode.Bounds.W = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("H"), Val)) OutNode.Bounds.H = (float)Val;
	}

	// Parse Slot data.
	const TSharedPtr<FJsonObject>* SlotObject;
	if (JsonObject->TryGetObjectField(TEXT("Slot"), SlotObject))
	{
		ParseSlotData(*SlotObject, OutNode.Slot);
	}

	// Parse TextStyle (v2.4).
	const TSharedPtr<FJsonObject>* TSObject;
	if (JsonObject->TryGetObjectField(TEXT("TextStyle"), TSObject))
	{
		double Val;
		if ((*TSObject)->TryGetNumberField(UMGPropertyKeys::FontSize, Val))
			OutNode.TextStyle.FontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(TEXT("SourceFontSize"), Val))
			OutNode.TextStyle.SourceFontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(UMGPropertyKeys::LetterSpacing, Val))
		{
			OutNode.TextStyle.LetterSpacing = (float)Val;
		}
	}

	// Parse WidgetStyle
	const TSharedPtr<FJsonObject>* WidgetStyleObject;
	if (JsonObject->TryGetObjectField(TEXT("WidgetStyle"), WidgetStyleObject))
	{
		double Val = 0.0;
		FString Str;
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::WidthOverride, Val))
		{
			OutNode.WidgetStyle.WidthOverride = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::HeightOverride, Val))
		{
			OutNode.WidgetStyle.HeightOverride = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::MinDesiredWidth, Val))
		{
			OutNode.WidgetStyle.MinDesiredWidth = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::MinDesiredHeight, Val))
		{
			OutNode.WidgetStyle.MinDesiredHeight = (float)Val;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::Padding); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.Padding = ParsePaddingValue(JsonValue);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::InnerSlotPadding, Str))
		{
			OutNode.WidgetStyle.InnerSlotPadding = FUMGValueParser::ParseVector2D(Str);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::SlotPadding); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.SlotPadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::StrokePadding); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.StrokePadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::StrokeMargin); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.StrokeMargin = ParsePaddingValue(JsonValue);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ImageMode, Str))
		{
			OutNode.WidgetStyle.ImageMode = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ImageTransform, Str))
		{
			OutNode.WidgetStyle.ImageTransform = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::Stretch, Str))
		{
			OutNode.WidgetStyle.Stretch = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::StretchDirection, Str))
		{
			OutNode.WidgetStyle.StretchDirection = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::BackgroundColor, Str))
		{
			OutNode.WidgetStyle.BackgroundColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::HoveredColor, Str))
		{
			OutNode.WidgetStyle.HoveredColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::PressedColor, Str))
		{
			OutNode.WidgetStyle.PressedColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::NamedSlot, Str))
		{
			OutNode.WidgetStyle.NamedSlot = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::SlotName, Str))
		{
			OutNode.WidgetStyle.SlotName = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::Visibility, Str))
		{
			OutNode.WidgetStyle.Visibility = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ToolTipText, Str))
		{
			OutNode.WidgetStyle.ToolTipText = Str;
		}
		bool BoolValue = false;
		if ((*WidgetStyleObject)->TryGetBoolField(UMGPropertyKeys::IsEnabled, BoolValue))
		{
			OutNode.WidgetStyle.IsEnabled = BoolValue;
		}
	}

	// Parse TextData
	const TSharedPtr<FJsonObject>* TextDataObject;
	if (JsonObject->TryGetObjectField(TEXT("TextData"), TextDataObject))
	{
		double Val = 0.0;
		FString Str;
		bool BoolValue = false;

		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Text, Str))
		{
			OutNode.Text.Text = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::RichTextStyleRows, Str))
		{
			OutNode.Text.RichTextStyleRowsJson = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::HintText, Str))
		{
			OutNode.Text.HintText = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::FontFamily, Str))
		{
			OutNode.Text.FontFamily = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TypefaceFontName, Str))
		{
			OutNode.Text.TypefaceFontName = Str;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("FontStyle"), Str))
		{
			OutNode.Text.FontStyle = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Justification, Str))
		{
			OutNode.Text.Justification = Str;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::AutoWrapText, BoolValue))
		{
			OutNode.Text.bAutoWrapText = BoolValue;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::WrapTextAt, Val))
		{
			OutNode.Text.WrapTextAt = (float)Val;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::LineHeightPercentage, Val))
		{
			OutNode.Text.LineHeightPercentage = (float)Val;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Underline, BoolValue))
		{
			OutNode.Text.bUnderline = BoolValue;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Strikethrough, BoolValue))
		{
			OutNode.Text.bStrikethrough = BoolValue;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Href, Str))
		{
			OutNode.Text.Href = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TextAlignVertical, Str))
		{
			OutNode.Text.TextAlignVertical = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::IconName, Str))
		{
			OutNode.Text.IconName = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
		{
			OutNode.Text.Color = FUMGValueParser::ParseColor(Str);
		}
	}

	// Parse Brush (v2.4).
	const TSharedPtr<FJsonObject>* BrushObject;
	if (JsonObject->TryGetObjectField(TEXT("Brush"), BrushObject))
	{
		double Val;
		FString Str;
		if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::OutlineCornerRadius, Val))
			OutNode.Brush.OutlineCornerRadius = (float)Val;
		if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::OutlineThickness, Val))
		{
			OutNode.Brush.OutlineThickness = (float)Val;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::DrawAs, Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (Design2UMGWidgetNodeAccess::TryParseDrawAs(Str, DrawAs))
			{
				OutNode.Brush.DrawAs = DrawAs;
			}
		}
		else if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::DrawAs, Val))
		{
			OutNode.Brush.DrawAs = (ESlateBrushDrawType::Type)(int32)Val;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::BrushColor, Str))
		{
			OutNode.Brush.BrushColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::OutlineColor, Str))
		{
			OutNode.Brush.OutlineColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialRef, Str))
		{
			OutNode.Brush.MaterialRef = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialParams, Str))
		{
			OutNode.Brush.MaterialParams = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::TexturePath, Str))
		{
			OutNode.Brush.TexturePath = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::CornerRadii, Str))
		{
			TArray<FString> Parts;
			Str.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				OutNode.Brush.CornerRadii = FVector4(
					FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
			}
		}
		const TSharedPtr<FJsonObject>* ImageObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Image"), ImageObject))
		{
			if ((*ImageObject)->TryGetStringField(UMGPropertyKeys::TexturePath, Str))
			{
				OutNode.Brush.Image.TexturePath = Str;
			}
			if ((*ImageObject)->TryGetStringField(JsonModeKey, Str))
			{
				OutNode.Brush.Image.Mode = Str;
			}
			if ((*ImageObject)->TryGetStringField(JsonTransformKey, Str)
				&& [&]()
				{
					FUMGVisualBrushData::FImageTransformData Transform;
					if (!Design2UMGWidgetNodeAccess::TryParseImageTransform(Str, Transform))
					{
						return false;
					}
					OutNode.Brush.Image.Transform = Transform;
					return true;
				}())
			{
			}
		}
		FString BrushTextureDiagnostic;
		if (Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(OutNode.Brush, &BrushTextureDiagnostic)
			&& !BrushTextureDiagnostic.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: %s"), *BrushTextureDiagnostic);
		}
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Gradient"), GradientObject))
		{
			if ((*GradientObject)->TryGetStringField(JsonTypeKey, Str))
			{
				OutNode.Brush.Gradient.Type = Str;
			}
			if ((*GradientObject)->TryGetNumberField(JsonRotationKey, Val))
			{
				OutNode.Brush.Gradient.Rotation = (float)Val;
			}
			if ((*GradientObject)->TryGetStringField(JsonCenterKey, Str))
			{
				OutNode.Brush.Gradient.Center = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
			}
			if ((*GradientObject)->TryGetStringField(JsonSizeKey, Str))
			{
				OutNode.Brush.Gradient.Size = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
			}
			const TArray<TSharedPtr<FJsonValue>>* StopArray = nullptr;
			if ((*GradientObject)->TryGetArrayField(JsonStopsKey, StopArray))
			{
				for (const TSharedPtr<FJsonValue>& StopValue : *StopArray)
				{
					const TSharedPtr<FJsonObject>* StopObject = nullptr;
					if (!StopValue->TryGetObject(StopObject) || !StopObject || !(*StopObject).IsValid())
					{
						continue;
					}

					FUMGVisualBrushData::FGradientStopData& Stop = OutNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
					if ((*StopObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
					{
						Stop.Color = FUMGValueParser::ParseColor(Str);
					}
					if ((*StopObject)->TryGetNumberField(JsonPositionKey, Val))
					{
						Stop.Position = (float)Val;
					}
				}
			}
		}
	}

	// Parse Effect (v2.5)
	const TSharedPtr<FJsonObject>* EffectObject;
	if (JsonObject->TryGetObjectField(TEXT("Effect"), EffectObject))
	{
		double Val;
		FString Str;
		bool BoolValue = false;
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::BlurStrength, Val))
		{
			OutNode.Effect.BlurStrength = (float)Val;
		}
		if ((*EffectObject)->TryGetBoolField(UMGPropertyKeys::RetainRendering, BoolValue))
		{
			OutNode.Effect.bRetainRendering = BoolValue;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectMaterial, Str))
		{
			OutNode.Effect.EffectMaterial = Str;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::TextureParameter, Str))
		{
			OutNode.Effect.TextureParameter = Str;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectBlurUVStep, Str))
		{
			OutNode.Effect.EffectBlurUVStep = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectColor, Str))
		{
			OutNode.Effect.EffectColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectOffset, Str))
		{
			OutNode.Effect.EffectOffset = FUMGValueParser::ParseVector2D(Str);
		}
		FUMGEffectData::FRenderingPhaseData RenderingPhase = OutNode.Effect.RenderingPhase.Get(FUMGEffectData::FRenderingPhaseData());
		bool bRenderingPhaseSet = false;
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::Phase, Val))
		{
			RenderingPhase.Phase = (int32)Val;
			bRenderingPhaseSet = true;
		}
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::PhaseCount, Val))
		{
			RenderingPhase.PhaseCount = FMath::Max(1, (int32)Val);
			bRenderingPhaseSet = true;
		}
		if (bRenderingPhaseSet)
		{
			OutNode.Effect.RenderingPhase = RenderingPhase;
		}
	}

	// Parse PathData
	const TSharedPtr<FJsonObject>* PathDataObject;
	if (JsonObject->TryGetObjectField(TEXT("PathData"), PathDataObject))
	{
		double Val = 0.0;
		FString Str;

		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathGeometry, Str))
		{
			OutNode.Path.Geometry = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillRule, Str))
		{
			OutNode.Path.FillRule = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillColor, Str))
		{
			OutNode.Path.FillColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeColor, Str))
		{
			OutNode.Path.StrokeColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*PathDataObject)->TryGetNumberField(UMGPropertyKeys::PathStrokeThickness, Val))
		{
			OutNode.Path.StrokeThickness = (float)Val;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeCap, Str))
		{
			OutNode.Path.StrokeCap = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeJoin, Str))
		{
			OutNode.Path.StrokeJoin = Str;
		}
	}

	// Parse ShadowData
	const TSharedPtr<FJsonObject>* ShadowDataObject;
	if (JsonObject->TryGetObjectField(TEXT("ShadowData"), ShadowDataObject))
	{
		double Val = 0.0;
		FString Str;
		const TArray<TSharedPtr<FJsonValue>>* BoxShadowArray = nullptr;

		if ((*ShadowDataObject)->TryGetStringField(UMGPropertyKeys::ShadowOffset, Str))
		{
			OutNode.Shadow.ShadowOffset = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*ShadowDataObject)->TryGetStringField(UMGPropertyKeys::ShadowColorAndOpacity, Str))
		{
			OutNode.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
		}
		if ((*ShadowDataObject)->TryGetNumberField(UMGPropertyKeys::ShadowBlur, Val))
		{
			OutNode.Shadow.ShadowBlur = (float)Val;
		}
		if ((*ShadowDataObject)->TryGetArrayField(JsonBoxShadowsKey, BoxShadowArray))
		{
			for (const TSharedPtr<FJsonValue>& LayerValue : *BoxShadowArray)
			{
				const TSharedPtr<FJsonObject>* LayerObject = nullptr;
				if (!LayerValue->TryGetObject(LayerObject) || !LayerObject || !(*LayerObject).IsValid())
				{
					continue;
				}

				FUMGBoxShadowLayerData Layer;
				if ((*LayerObject)->TryGetStringField(JsonOffsetKey, Str))
				{
					Layer.Offset = FUMGValueParser::ParseVector2D(Str);
				}
				(*LayerObject)->TryGetNumberField(JsonBlurKey, Val);
				Layer.Blur = (float)Val;
				(*LayerObject)->TryGetNumberField(JsonSpreadKey, Val);
				Layer.Spread = (float)Val;
				if ((*LayerObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
				{
					Layer.Color = FUMGValueParser::ParseColor(Str);
				}
				OutNode.Shadow.BoxShadows.Add(Layer);
			}
		}
	}

	// Parse Render (v2.7)
	const TSharedPtr<FJsonObject>* RenderObject;
	if (JsonObject->TryGetObjectField(TEXT("Render"), RenderObject))
	{
		double Val;
		FString Str;
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::Clipping, Str))
		{
			OutNode.Render.Clipping = Str;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderOpacity, Val))
		{
			OutNode.Render.RenderOpacity = (float)Val;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderAngle, Val))
		{
			OutNode.Render.RenderAngle = (float)Val;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformPivot, Str))
		{
			OutNode.Render.RenderTransformPivot = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformScale, Str))
		{
			OutNode.Render.RenderTransformScale = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTranslation, Str))
		{
			OutNode.Render.RenderTranslation = FUMGValueParser::ParseVector2D(Str);
		}
	}

	// Parse WidgetState (v2.6)
	const TSharedPtr<FJsonObject>* WidgetStateObject;
	if (JsonObject->TryGetObjectField(TEXT("WidgetState"), WidgetStateObject))
	{
		double Val;
		if ((*WidgetStateObject)->TryGetNumberField(UMGPropertyKeys::ActiveWidgetIndex, Val))
		{
			OutNode.WidgetState.ActiveWidgetIndex = static_cast<int32>(Val);
		}
	}

	// Parse ScrollBox (v2.6)
	const TSharedPtr<FJsonObject>* ScrollBoxObject;
	if (JsonObject->TryGetObjectField(TEXT("ScrollBox"), ScrollBoxObject))
	{
		FString Str;
		if ((*ScrollBoxObject)->TryGetStringField(UMGPropertyKeys::Orientation, Str))
		{
			OutNode.ScrollBox.Orientation = Str;
		}
		if ((*ScrollBoxObject)->TryGetStringField(UMGPropertyKeys::ScrollBarVisibility, Str))
		{
			OutNode.ScrollBox.ScrollBarVisibility = Str;
		}
	}

	// Parse Children in the standard format.
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray;
	if (JsonObject->TryGetArrayField(TEXT("Children"), ChildrenArray))
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
		{
			const TSharedPtr<FJsonObject>* ChildObject;
			if (ChildValue->TryGetObject(ChildObject))
			{
				FUMGWidgetNode ChildNode;
				if (ParseWidgetNode(*ChildObject, ChildNode))
				{
					OutNode.Children.Add(ChildNode);
				}
			}
		}
	}

	return true;
}

void FUMGJsonParser::ParseSlotData(const TSharedPtr<FJsonObject>& JsonObject, FUMGSlotData& OutSlot)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// Parse Anchors (CanvasPanel).
	const TSharedPtr<FJsonObject>* AnchorsObject;
	if (JsonObject->TryGetObjectField(TEXT("Anchors"), AnchorsObject))
	{
		ParseAnchorsData(*AnchorsObject, OutSlot.Anchors);
	}

	// Parse Alignment in either object or array form.
	if (JsonObject->HasField(TEXT("Alignment")))
	{
		const TSharedPtr<FJsonValue>& AlignValue = JsonObject->Values.FindRef(TEXT("Alignment"));
		if (AlignValue->Type == EJson::Object)
		{
			ParseAnchorData(AlignValue->AsObject(), OutSlot.Alignment);
		}
		else if (AlignValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = AlignValue->AsArray();
			if (Arr.Num() >= 2)
			{
				OutSlot.Alignment.X = Arr[0]->AsNumber();
				OutSlot.Alignment.Y = Arr[1]->AsNumber();
			}
		}
	}

	// Parse Offsets (CanvasPanel).
	const TSharedPtr<FJsonObject>* OffsetsObject;
	if (JsonObject->TryGetObjectField(TEXT("Offsets"), OffsetsObject))
	{
		ParseOffsetsData(*OffsetsObject, OutSlot.Offsets);
	}

	// Parse Padding.
	if (JsonObject->HasField(UMGPropertyKeys::Padding))
	{
		OutSlot.Padding = ParsePaddingValue(JsonObject->Values.FindRef(UMGPropertyKeys::Padding));
	}

	// Parse HorizontalAlignment (VerticalBox/HorizontalBox/Overlay).
	FString HAlign;
	if (JsonObject->TryGetStringField(TEXT("HAlign"), HAlign) ||
		JsonObject->TryGetStringField(TEXT("HorizontalAlignment"), HAlign))
	{
		OutSlot.HorizontalAlignment = FUMGValueParser::ParseSlotHorizontalAlignment(HAlign);
	}

	// Parse VerticalAlignment.
	FString VAlign;
	if (JsonObject->TryGetStringField(TEXT("VAlign"), VAlign) ||
		JsonObject->TryGetStringField(TEXT("VerticalAlignment"), VAlign))
	{
		OutSlot.VerticalAlignment = FUMGValueParser::ParseSlotVerticalAlignment(VAlign);
	}

	// Parse SizeRule (Auto/Fill).
	FString SizeRule;
	if (JsonObject->TryGetStringField(TEXT("SizeRule"), SizeRule))
	{
		OutSlot.SizeRule = FUMGValueParser::ParseSlotSizeRule(SizeRule);
	}

	// Parse FillWeight.
	double FillWeight;
	if (JsonObject->TryGetNumberField(TEXT("FillWeight"), FillWeight))
	{
		OutSlot.FillWeight = static_cast<float>(FillWeight);
	}

	// Parse Grid-related fields.
	int32 IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Row"), IntValue)) OutSlot.Row = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Column"), IntValue)) OutSlot.Column = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("RowSpan"), IntValue)) OutSlot.RowSpan = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("ColumnSpan"), IntValue)) OutSlot.ColumnSpan = IntValue;

	// Parse bAutoSize (Size To Content).
	bool bAutoSize = false;
	if (JsonObject->TryGetBoolField(TEXT("bAutoSize"), bAutoSize))
	{
		OutSlot.bAutoSize = bAutoSize;
	}
}

void FUMGJsonParser::ParseAnchorsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGAnchorsData& OutAnchors)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// Parse Min in either object or array form.
	if (JsonObject->HasField(TEXT("Min")))
	{
		const TSharedPtr<FJsonValue>& Value = JsonObject->Values.FindRef(TEXT("Min"));
		if (Value->Type == EJson::Object)
		{
			ParseAnchorData(Value->AsObject(), OutAnchors.Min);
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() >= 2)
			{
				OutAnchors.Min.X = Arr[0]->AsNumber();
				OutAnchors.Min.Y = Arr[1]->AsNumber();
			}
		}
	}

	// Parse Max in either object or array form.
	if (JsonObject->HasField(TEXT("Max")))
	{
		const TSharedPtr<FJsonValue>& Value = JsonObject->Values.FindRef(TEXT("Max"));
		if (Value->Type == EJson::Object)
		{
			ParseAnchorData(Value->AsObject(), OutAnchors.Max);
		}
		else if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() >= 2)
			{
				OutAnchors.Max.X = Arr[0]->AsNumber();
				OutAnchors.Max.Y = Arr[1]->AsNumber();
			}
		}
	}
}

void FUMGJsonParser::ParseAnchorData(const TSharedPtr<FJsonObject>& JsonObject, FUMGAnchorData& OutAnchor)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	JsonObject->TryGetNumberField(TEXT("X"), OutAnchor.X);
	JsonObject->TryGetNumberField(TEXT("Y"), OutAnchor.Y);
}

void FUMGJsonParser::ParseOffsetsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGOffsetsData& OutOffsets)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	JsonObject->TryGetNumberField(TEXT("Left"), OutOffsets.Left);
	JsonObject->TryGetNumberField(TEXT("Top"), OutOffsets.Top);
	JsonObject->TryGetNumberField(TEXT("Right"), OutOffsets.Right);
	JsonObject->TryGetNumberField(TEXT("Bottom"), OutOffsets.Bottom);
}
