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
	constexpr TCHAR JsonStartPointKey[] = TEXT("StartPoint");
	constexpr TCHAR JsonEndPointKey[] = TEXT("EndPoint");
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

	bool TryParseVector4(const FString& Value, FVector4& OutValue)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 4)
		{
			return false;
		}

		float Parsed[4] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parsed); ++Index)
		{
			if (!LexTryParseString(Parsed[Index], *Parts[Index].TrimStartAndEnd()))
			{
				return false;
			}
		}
		OutValue = FVector4(Parsed[0], Parsed[1], Parsed[2], Parsed[3]);
		return true;
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

	// Parse the LayoutMapper justify-spacer marker (absent in older JSON means false).
	JsonObject->TryGetBoolField(TEXT("SyntheticJustifySpacer"), OutNode.bSyntheticJustifySpacer);

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

	// Parse each typed payload section; the payload parsers read their own section key.
	ParseTextStylePayload(JsonObject, OutNode.TextStyle);
	ParseWidgetStylePayload(JsonObject, OutNode.WidgetStyle);
	ParseTextPayload(JsonObject, OutNode.Text);
	ParseBrushPayload(JsonObject, OutNode.Brush);
	ParseEffectPayload(JsonObject, OutNode.Effect);
	ParsePathPayload(JsonObject, OutNode.Path);
	ParseShadowPayload(JsonObject, OutNode.Shadow);
	ParseRenderPayload(JsonObject, OutNode.Render);
	ParseWidgetStatePayload(JsonObject, OutNode.WidgetState);
	ParseScrollBoxPayload(JsonObject, OutNode.ScrollBox);

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

void FUMGJsonParser::ParseTextStylePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGTextStyleData& OutTextStyle)
{
	// Parse TextStyle (v2.4).
	const TSharedPtr<FJsonObject>* TSObject;
	if (NodeObject->TryGetObjectField(TEXT("TextStyle"), TSObject))
	{
		double Val;
		if ((*TSObject)->TryGetNumberField(UMGPropertyKeys::FontSize, Val))
			OutTextStyle.FontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(TEXT("SourceFontSize"), Val))
			OutTextStyle.SourceFontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(UMGPropertyKeys::LetterSpacing, Val))
		{
			OutTextStyle.LetterSpacing = (float)Val;
		}
	}
}

void FUMGJsonParser::ParseWidgetStylePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGWidgetStyleData& OutWidgetStyle)
{
	// Parse WidgetStyle
	const TSharedPtr<FJsonObject>* WidgetStyleObject;
	if (NodeObject->TryGetObjectField(TEXT("WidgetStyle"), WidgetStyleObject))
	{
		double Val = 0.0;
		FString Str;
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::WidthOverride, Val))
		{
			OutWidgetStyle.WidthOverride = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::HeightOverride, Val))
		{
			OutWidgetStyle.HeightOverride = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::MinDesiredWidth, Val))
		{
			OutWidgetStyle.MinDesiredWidth = (float)Val;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(UMGPropertyKeys::MinDesiredHeight, Val))
		{
			OutWidgetStyle.MinDesiredHeight = (float)Val;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::Padding); JsonValue.IsValid())
		{
			OutWidgetStyle.Padding = ParsePaddingValue(JsonValue);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::InnerSlotPadding, Str))
		{
			OutWidgetStyle.InnerSlotPadding = FUMGValueParser::ParseVector2D(Str);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::SlotPadding); JsonValue.IsValid())
		{
			OutWidgetStyle.SlotPadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::StrokePadding); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokePadding = ParsePaddingValue(JsonValue);
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(UMGPropertyKeys::StrokeMargin); JsonValue.IsValid())
		{
			OutWidgetStyle.StrokeMargin = ParsePaddingValue(JsonValue);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ImageMode, Str))
		{
			OutWidgetStyle.ImageMode = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ImageTransform, Str))
		{
			OutWidgetStyle.ImageTransform = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::Stretch, Str))
		{
			OutWidgetStyle.Stretch = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::StretchDirection, Str))
		{
			OutWidgetStyle.StretchDirection = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::BackgroundColor, Str))
		{
			OutWidgetStyle.BackgroundColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::HoveredColor, Str))
		{
			OutWidgetStyle.HoveredColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::PressedColor, Str))
		{
			OutWidgetStyle.PressedColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::NamedSlot, Str))
		{
			OutWidgetStyle.NamedSlot = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::SlotName, Str))
		{
			OutWidgetStyle.SlotName = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::Visibility, Str))
		{
			OutWidgetStyle.Visibility = Str;
		}
		if ((*WidgetStyleObject)->TryGetStringField(UMGPropertyKeys::ToolTipText, Str))
		{
			OutWidgetStyle.ToolTipText = Str;
		}
		bool BoolValue = false;
		if ((*WidgetStyleObject)->TryGetBoolField(UMGPropertyKeys::IsEnabled, BoolValue))
		{
			OutWidgetStyle.IsEnabled = BoolValue;
		}
	}
}

void FUMGJsonParser::ParseTextPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGTextData& OutText)
{
	// Parse TextData
	const TSharedPtr<FJsonObject>* TextDataObject;
	if (NodeObject->TryGetObjectField(TEXT("TextData"), TextDataObject))
	{
		double Val = 0.0;
		FString Str;
		bool BoolValue = false;

		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Text, Str))
		{
			OutText.Text = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::RichTextStyleRows, Str))
		{
			OutText.RichTextStyleRowsJson = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::HintText, Str))
		{
			OutText.HintText = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::FontFamily, Str))
		{
			OutText.FontFamily = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TypefaceFontName, Str))
		{
			OutText.TypefaceFontName = Str;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("FontStyle"), Str))
		{
			OutText.FontStyle = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Justification, Str))
		{
			OutText.Justification = Str;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::AutoWrapText, BoolValue))
		{
			OutText.bAutoWrapText = BoolValue;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::WrapTextAt, Val))
		{
			OutText.WrapTextAt = (float)Val;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::LineHeightPercentage, Val))
		{
			OutText.LineHeightPercentage = (float)Val;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::LineHeightBaselineShift, Val))
		{
			OutText.LineHeightBaselineShift = (float)Val;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Underline, BoolValue))
		{
			OutText.bUnderline = BoolValue;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Strikethrough, BoolValue))
		{
			OutText.bStrikethrough = BoolValue;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Href, Str))
		{
			OutText.Href = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TextAlignVertical, Str))
		{
			OutText.TextAlignVertical = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::IconName, Str))
		{
			OutText.IconName = Str;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
		{
			OutText.Color = FUMGValueParser::ParseColor(Str);
		}
	}
}

void FUMGJsonParser::ParseBrushPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGVisualBrushData& OutBrush)
{
	// Parse Brush (v2.4).
	const TSharedPtr<FJsonObject>* BrushObject;
	if (NodeObject->TryGetObjectField(TEXT("Brush"), BrushObject))
	{
		double Val;
		FString Str;
		// Negative radius is the legacy "unset" encoding; keep it out of the
		// optional so IsSet() preserves the old >= 0 consumer contract.
		if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::OutlineCornerRadius, Val) && Val >= 0.0)
			OutBrush.OutlineCornerRadius = (float)Val;
		if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::OutlineThickness, Val))
		{
			OutBrush.OutlineThickness = (float)Val;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::DrawAs, Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (Design2UMGWidgetNodeAccess::TryParseDrawAs(Str, DrawAs))
			{
				OutBrush.DrawAs = DrawAs;
			}
		}
		else if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::DrawAs, Val))
		{
			OutBrush.DrawAs = (ESlateBrushDrawType::Type)(int32)Val;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::BrushColor, Str))
		{
			OutBrush.BrushColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::OutlineColor, Str))
		{
			OutBrush.OutlineColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialRef, Str))
		{
			OutBrush.MaterialRef = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialParams, Str))
		{
			OutBrush.MaterialParams = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::TexturePath, Str))
		{
			OutBrush.TexturePath = Str;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::CornerRadii, Str))
		{
			TArray<FString> Parts;
			Str.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 4)
			{
				const FVector4 Radii(
					FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
				// Legacy "unset" contract only tested the first component for
				// negativity; keep that exact behavior for IsSet().
				if (Radii.X >= 0.0f)
				{
					OutBrush.CornerRadii = Radii;
				}
			}
		}
		const TSharedPtr<FJsonObject>* ImageObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Image"), ImageObject))
		{
			if ((*ImageObject)->TryGetStringField(UMGPropertyKeys::TexturePath, Str))
			{
				OutBrush.Image.TexturePath = Str;
			}
			if ((*ImageObject)->TryGetStringField(JsonModeKey, Str))
			{
				OutBrush.Image.Mode = Str;
			}
			if ((*ImageObject)->TryGetStringField(JsonTransformKey, Str)
				&& [&]()
				{
					FUMGVisualBrushData::FImageTransformData Transform;
					if (!Design2UMGWidgetNodeAccess::TryParseImageTransform(Str, Transform))
					{
						return false;
					}
					OutBrush.Image.Transform = Transform;
					return true;
				}())
			{
			}
		}
		FString BrushTextureDiagnostic;
		if (Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(OutBrush, &BrushTextureDiagnostic)
			&& !BrushTextureDiagnostic.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FUMGJsonParser: %s"), *BrushTextureDiagnostic);
		}
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Gradient"), GradientObject))
		{
			if ((*GradientObject)->TryGetStringField(JsonTypeKey, Str))
			{
				OutBrush.Gradient.Type = Str;
			}
			if ((*GradientObject)->TryGetNumberField(JsonRotationKey, Val))
			{
				OutBrush.Gradient.Rotation = (float)Val;
			}
			if ((*GradientObject)->TryGetStringField(JsonStartPointKey, Str))
			{
				OutBrush.Gradient.StartPoint = FUMGValueParser::ParseVector2D(Str, FVector2D::ZeroVector);
			}
			if ((*GradientObject)->TryGetStringField(JsonEndPointKey, Str))
			{
				OutBrush.Gradient.EndPoint = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 0.0f));
			}
			if ((*GradientObject)->TryGetStringField(JsonCenterKey, Str))
			{
				OutBrush.Gradient.Center = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
			}
			if ((*GradientObject)->TryGetStringField(JsonSizeKey, Str))
			{
				OutBrush.Gradient.Size = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
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

					FUMGVisualBrushData::FGradientStopData& Stop = OutBrush.Gradient.Stops.AddDefaulted_GetRef();
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
}

void FUMGJsonParser::ParseEffectPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGEffectData& OutEffect)
{
	// Parse Effect (v2.5)
	const TSharedPtr<FJsonObject>* EffectObject;
	if (NodeObject->TryGetObjectField(TEXT("Effect"), EffectObject))
	{
		double Val;
		FString Str;
		bool BoolValue = false;
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::BlurStrength, Val))
		{
			OutEffect.BlurStrength = (float)Val;
		}
		if ((*EffectObject)->TryGetBoolField(UMGPropertyKeys::RetainRendering, BoolValue))
		{
			OutEffect.bRetainRendering = BoolValue;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectMaterial, Str))
		{
			OutEffect.EffectMaterial = Str;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::TextureParameter, Str))
		{
			OutEffect.TextureParameter = Str;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectBlurUVStep, Str))
		{
			OutEffect.EffectBlurUVStep = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectColor, Str))
		{
			OutEffect.EffectColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectOffset, Str))
		{
			OutEffect.EffectOffset = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::GeometryMaskShape, Str))
		{
			if (Str.Equals(TEXT("Ellipse"), ESearchCase::IgnoreCase))
			{
				OutEffect.GeometryMaskShape = EUMGGeometryMaskShape::Ellipse;
			}
			else if (Str.Equals(TEXT("RoundedRectangle"), ESearchCase::IgnoreCase))
			{
				OutEffect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
			}
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::GeometryMaskSize, Str))
		{
			OutEffect.GeometryMaskSize = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::GeometryMaskCornerRadii, Str))
		{
			FVector4 Radii;
			if (TryParseVector4(Str, Radii))
			{
				OutEffect.GeometryMaskCornerRadii = Radii;
			}
		}
		FUMGEffectData::FRenderingPhaseData RenderingPhase = OutEffect.RenderingPhase.Get(FUMGEffectData::FRenderingPhaseData());
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
			OutEffect.RenderingPhase = RenderingPhase;
		}
	}
}

void FUMGJsonParser::ParsePathPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGPathData& OutPath)
{
	// Parse PathData
	const TSharedPtr<FJsonObject>* PathDataObject;
	if (NodeObject->TryGetObjectField(TEXT("PathData"), PathDataObject))
	{
		double Val = 0.0;
		FString Str;

		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathGeometry, Str))
		{
			OutPath.Geometry = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillRule, Str))
		{
			OutPath.FillRule = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillColor, Str))
		{
			OutPath.FillColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeColor, Str))
		{
			OutPath.StrokeColor = FUMGValueParser::ParseColor(Str);
		}
		if ((*PathDataObject)->TryGetNumberField(UMGPropertyKeys::PathStrokeThickness, Val))
		{
			OutPath.StrokeThickness = (float)Val;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeCap, Str))
		{
			OutPath.StrokeCap = Str;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeJoin, Str))
		{
			OutPath.StrokeJoin = Str;
		}
	}
}

void FUMGJsonParser::ParseShadowPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGShadowData& OutShadow)
{
	// Parse ShadowData
	const TSharedPtr<FJsonObject>* ShadowDataObject;
	if (NodeObject->TryGetObjectField(TEXT("ShadowData"), ShadowDataObject))
	{
		double Val = 0.0;
		FString Str;
		const TArray<TSharedPtr<FJsonValue>>* BoxShadowArray = nullptr;

		if ((*ShadowDataObject)->TryGetStringField(UMGPropertyKeys::ShadowOffset, Str))
		{
			OutShadow.ShadowOffset = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*ShadowDataObject)->TryGetStringField(UMGPropertyKeys::ShadowColorAndOpacity, Str))
		{
			OutShadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
		}
		if ((*ShadowDataObject)->TryGetNumberField(UMGPropertyKeys::ShadowBlur, Val))
		{
			OutShadow.ShadowBlur = (float)Val;
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
				if ((*LayerObject)->TryGetNumberField(JsonBlurKey, Val))
				{
					Layer.Blur = (float)Val;
				}
				if ((*LayerObject)->TryGetNumberField(JsonSpreadKey, Val))
				{
					Layer.Spread = (float)Val;
				}
				if ((*LayerObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
				{
					Layer.Color = FUMGValueParser::ParseColor(Str);
				}
				OutShadow.BoxShadows.Add(Layer);
			}
		}
	}
}

void FUMGJsonParser::ParseRenderPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGRenderStyleData& OutRender)
{
	// Parse Render (v2.7)
	const TSharedPtr<FJsonObject>* RenderObject;
	if (NodeObject->TryGetObjectField(TEXT("Render"), RenderObject))
	{
		double Val;
		FString Str;
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::Clipping, Str))
		{
			OutRender.Clipping = Str;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderOpacity, Val))
		{
			OutRender.RenderOpacity = (float)Val;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderAngle, Val))
		{
			OutRender.RenderAngle = (float)Val;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformPivot, Str))
		{
			OutRender.RenderTransformPivot = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformScale, Str))
		{
			OutRender.RenderTransformScale = FUMGValueParser::ParseVector2D(Str);
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTranslation, Str))
		{
			OutRender.RenderTranslation = FUMGValueParser::ParseVector2D(Str);
		}
	}
}

void FUMGJsonParser::ParseWidgetStatePayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGWidgetStateData& OutWidgetState)
{
	// Parse WidgetState (v2.6)
	const TSharedPtr<FJsonObject>* WidgetStateObject;
	if (NodeObject->TryGetObjectField(TEXT("WidgetState"), WidgetStateObject))
	{
		double Val;
		if ((*WidgetStateObject)->TryGetNumberField(UMGPropertyKeys::ActiveWidgetIndex, Val))
		{
			OutWidgetState.ActiveWidgetIndex = static_cast<int32>(Val);
		}
	}
}

void FUMGJsonParser::ParseScrollBoxPayload(const TSharedPtr<FJsonObject>& NodeObject, FUMGScrollBoxData& OutScrollBox)
{
	// Parse ScrollBox (v2.6)
	const TSharedPtr<FJsonObject>* ScrollBoxObject;
	if (NodeObject->TryGetObjectField(TEXT("ScrollBox"), ScrollBoxObject))
	{
		FString Str;
		if ((*ScrollBoxObject)->TryGetStringField(UMGPropertyKeys::Orientation, Str))
		{
			OutScrollBox.Orientation = Str;
		}
		if ((*ScrollBoxObject)->TryGetStringField(UMGPropertyKeys::ScrollBarVisibility, Str))
		{
			OutScrollBox.ScrollBarVisibility = Str;
		}
	}
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
