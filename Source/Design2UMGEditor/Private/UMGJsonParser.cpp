// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParser.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

// ============================================================================
// 辅助函数
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

	// 解析 Padding 值，支持单个数字和 "L,T,R,B" 字符串格式。
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

	bool TryParseJsonDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
	{
		static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
			{ TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType },
			{ TEXT("Box"), ESlateBrushDrawType::Box },
			{ TEXT("Border"), ESlateBrushDrawType::Border },
			{ TEXT("Image"), ESlateBrushDrawType::Image },
			{ TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox },
		};

		if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
		{
			OutDrawAs = *Found;
			return true;
		}

		return false;
	}

	bool TryParseImageTransform(const FString& Value, FUMGVisualBrushData::FImageTransformData& OutTransform)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 6)
		{
			return false;
		}

		OutTransform.A = FCString::Atof(*Parts[0]);
		OutTransform.B = FCString::Atof(*Parts[1]);
		OutTransform.TX = FCString::Atof(*Parts[2]);
		OutTransform.C = FCString::Atof(*Parts[3]);
		OutTransform.D = FCString::Atof(*Parts[4]);
		OutTransform.TY = FCString::Atof(*Parts[5]);
		return true;
	}
}

// ============================================================================
// 主解析逻辑
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

	// 解析 RootWidget
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

	// 解析 Bounds。v2.2 新增，用于归一化坐标。
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

	// 解析 TextStyle (v2.4)
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
			OutNode.TextStyle.bHasLetterSpacing = true;
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
			OutNode.Text.bHasText = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::RichTextStyleRows, Str))
		{
			OutNode.Text.RichTextStyleRowsJson = Str;
			OutNode.Text.bHasRichTextStyleRows = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::HintText, Str))
		{
			OutNode.Text.HintText = Str;
			OutNode.Text.bHasHintText = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::FontFamily, Str))
		{
			OutNode.Text.FontFamily = Str;
			OutNode.Text.bHasFontFamily = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TypefaceFontName, Str))
		{
			OutNode.Text.TypefaceFontName = Str;
			OutNode.Text.bHasTypefaceFontName = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("FontStyle"), Str))
		{
			OutNode.Text.FontStyle = Str;
			OutNode.Text.bHasFontStyle = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Justification, Str))
		{
			OutNode.Text.Justification = Str;
			OutNode.Text.bHasJustification = true;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::AutoWrapText, BoolValue))
		{
			OutNode.Text.bAutoWrapText = BoolValue;
			OutNode.Text.bHasAutoWrapText = true;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::WrapTextAt, Val))
		{
			OutNode.Text.WrapTextAt = (float)Val;
			OutNode.Text.bHasWrapTextAt = true;
		}
		if ((*TextDataObject)->TryGetNumberField(UMGPropertyKeys::LineHeightPercentage, Val))
		{
			OutNode.Text.LineHeightPercentage = (float)Val;
			OutNode.Text.bHasLineHeightPercentage = true;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Underline, BoolValue))
		{
			OutNode.Text.bUnderline = BoolValue;
			OutNode.Text.bHasUnderline = true;
		}
		if ((*TextDataObject)->TryGetBoolField(UMGPropertyKeys::Strikethrough, BoolValue))
		{
			OutNode.Text.bStrikethrough = BoolValue;
			OutNode.Text.bHasStrikethrough = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Href, Str))
		{
			OutNode.Text.Href = Str;
			OutNode.Text.bHasHref = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::TextAlignVertical, Str))
		{
			OutNode.Text.TextAlignVertical = Str;
			OutNode.Text.bHasTextAlignVertical = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::IconName, Str))
		{
			OutNode.Text.IconName = Str;
			OutNode.Text.bHasIconName = true;
		}
		if ((*TextDataObject)->TryGetStringField(UMGPropertyKeys::Color, Str))
		{
			OutNode.Text.Color = FUMGValueParser::ParseColor(Str);
			OutNode.Text.bHasColor = true;
		}
	}

	// 解析 Brush (v2.4)
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
			OutNode.Brush.bHasOutlineThickness = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::DrawAs, Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (TryParseJsonDrawAs(Str, DrawAs))
			{
				OutNode.Brush.DrawAs = DrawAs;
				OutNode.Brush.bHasDrawAs = true;
			}
		}
		else if ((*BrushObject)->TryGetNumberField(UMGPropertyKeys::DrawAs, Val))
		{
			OutNode.Brush.DrawAs = (ESlateBrushDrawType::Type)(int32)Val;
			OutNode.Brush.bHasDrawAs = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::BrushColor, Str))
		{
			OutNode.Brush.BrushColor = FUMGValueParser::ParseColor(Str);
			OutNode.Brush.bHasBrushColor = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::OutlineColor, Str))
		{
			OutNode.Brush.OutlineColor = FUMGValueParser::ParseColor(Str);
			OutNode.Brush.bHasOutlineColor = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialRef, Str))
		{
			OutNode.Brush.MaterialRef = Str;
			OutNode.Brush.bHasMaterialRef = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::MaterialParams, Str))
		{
			OutNode.Brush.MaterialParams = Str;
			OutNode.Brush.bHasMaterialParams = true;
		}
		if ((*BrushObject)->TryGetStringField(UMGPropertyKeys::TexturePath, Str))
		{
			OutNode.Brush.TexturePath = Str;
			OutNode.Brush.Image.TexturePath = Str;
			OutNode.Brush.Image.bHasTexturePath = true;
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
				OutNode.Brush.TexturePath = Str;
				OutNode.Brush.Image.TexturePath = Str;
				OutNode.Brush.Image.bHasTexturePath = true;
			}
			if ((*ImageObject)->TryGetStringField(JsonModeKey, Str))
			{
				OutNode.Brush.Image.Mode = Str;
				OutNode.Brush.Image.bHasMode = true;
			}
			if ((*ImageObject)->TryGetStringField(JsonTransformKey, Str)
				&& TryParseImageTransform(Str, OutNode.Brush.Image.Transform))
			{
				OutNode.Brush.Image.bHasTransform = true;
			}
		}
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Gradient"), GradientObject))
		{
			if ((*GradientObject)->TryGetStringField(JsonTypeKey, Str))
			{
				OutNode.Brush.Gradient.Type = Str;
				OutNode.Brush.Gradient.bHasType = true;
			}
			if ((*GradientObject)->TryGetNumberField(JsonRotationKey, Val))
			{
				OutNode.Brush.Gradient.Rotation = (float)Val;
				OutNode.Brush.Gradient.bHasRotation = true;
			}
			if ((*GradientObject)->TryGetStringField(JsonCenterKey, Str))
			{
				OutNode.Brush.Gradient.Center = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
				OutNode.Brush.Gradient.bHasCenter = true;
			}
			if ((*GradientObject)->TryGetStringField(JsonSizeKey, Str))
			{
				OutNode.Brush.Gradient.Size = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
				OutNode.Brush.Gradient.bHasSize = true;
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
			OutNode.Effect.bHasBlurStrength = true;
		}
		if ((*EffectObject)->TryGetBoolField(UMGPropertyKeys::RetainRendering, BoolValue))
		{
			OutNode.Effect.bRetainRendering = BoolValue;
			OutNode.Effect.bHasRetainRendering = true;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectMaterial, Str))
		{
			OutNode.Effect.EffectMaterial = Str;
			OutNode.Effect.bHasEffectMaterial = true;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::TextureParameter, Str))
		{
			OutNode.Effect.TextureParameter = Str;
			OutNode.Effect.bHasTextureParameter = true;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectBlurUVStep, Str))
		{
			OutNode.Effect.EffectBlurUVStep = FUMGValueParser::ParseVector2D(Str);
			OutNode.Effect.bHasEffectBlurUVStep = true;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectColor, Str))
		{
			OutNode.Effect.EffectColor = FUMGValueParser::ParseColor(Str);
			OutNode.Effect.bHasEffectColor = true;
		}
		if ((*EffectObject)->TryGetStringField(UMGPropertyKeys::EffectOffset, Str))
		{
			OutNode.Effect.EffectOffset = FUMGValueParser::ParseVector2D(Str);
			OutNode.Effect.bHasEffectOffset = true;
		}
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::Phase, Val))
		{
			OutNode.Effect.Phase = (int32)Val;
			OutNode.Effect.bHasRenderingPhase = true;
		}
		if ((*EffectObject)->TryGetNumberField(UMGPropertyKeys::PhaseCount, Val))
		{
			OutNode.Effect.PhaseCount = FMath::Max(1, (int32)Val);
			OutNode.Effect.bHasRenderingPhase = true;
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
			OutNode.Path.bHasGeometry = true;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillRule, Str))
		{
			OutNode.Path.FillRule = Str;
			OutNode.Path.bHasFillRule = true;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathFillColor, Str))
		{
			OutNode.Path.FillColor = FUMGValueParser::ParseColor(Str);
			OutNode.Path.bHasFillColor = true;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeColor, Str))
		{
			OutNode.Path.StrokeColor = FUMGValueParser::ParseColor(Str);
			OutNode.Path.bHasStrokeColor = true;
		}
		if ((*PathDataObject)->TryGetNumberField(UMGPropertyKeys::PathStrokeThickness, Val))
		{
			OutNode.Path.StrokeThickness = (float)Val;
			OutNode.Path.bHasStrokeThickness = true;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeCap, Str))
		{
			OutNode.Path.StrokeCap = Str;
			OutNode.Path.bHasStrokeCap = true;
		}
		if ((*PathDataObject)->TryGetStringField(UMGPropertyKeys::PathStrokeJoin, Str))
		{
			OutNode.Path.StrokeJoin = Str;
			OutNode.Path.bHasStrokeJoin = true;
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
			OutNode.Shadow.bHasShadowOffset = true;
		}
		if ((*ShadowDataObject)->TryGetStringField(UMGPropertyKeys::ShadowColorAndOpacity, Str))
		{
			OutNode.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
			OutNode.Shadow.bHasShadowColorAndOpacity = true;
		}
		if ((*ShadowDataObject)->TryGetNumberField(UMGPropertyKeys::ShadowBlur, Val))
		{
			OutNode.Shadow.ShadowBlur = (float)Val;
			OutNode.Shadow.bHasShadowBlur = true;
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
			OutNode.Render.bHasClipping = true;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderOpacity, Val))
		{
			OutNode.Render.RenderOpacity = (float)Val;
			OutNode.Render.bHasRenderOpacity = true;
		}
		if ((*RenderObject)->TryGetNumberField(UMGPropertyKeys::RenderAngle, Val))
		{
			OutNode.Render.RenderAngle = (float)Val;
			OutNode.Render.bHasRenderAngle = true;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformPivot, Str))
		{
			OutNode.Render.RenderTransformPivot = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
			OutNode.Render.bHasRenderTransformPivot = true;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderScale, Str))
		{
			OutNode.Render.RenderScale = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
			OutNode.Render.bHasRenderScale = true;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTransformScale, Str))
		{
			OutNode.Render.RenderTransformScale = FUMGValueParser::ParseVector2D(Str);
			OutNode.Render.bHasRenderTransformScale = true;
		}
		if ((*RenderObject)->TryGetStringField(UMGPropertyKeys::RenderTranslation, Str))
		{
			OutNode.Render.RenderTranslation = FUMGValueParser::ParseVector2D(Str);
			OutNode.Render.bHasRenderTranslation = true;
		}
	}

	// Parse WidgetState (v2.6)
	const TSharedPtr<FJsonObject>* WidgetStateObject;
	if (JsonObject->TryGetObjectField(TEXT("WidgetState"), WidgetStateObject))
	{
		double Val;
		if ((*WidgetStateObject)->TryGetNumberField(UMGPropertyKeys::ActiveWidgetIndex, Val))
		{
			OutNode.WidgetState.ActiveWidgetIndex = (int32)Val;
			OutNode.WidgetState.bHasActiveWidgetIndex = true;
		}
	}

	// 解析 Children，标准格式。
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

	// 解析 Anchors (CanvasPanel)
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

	// 解析 Offsets (CanvasPanel)
	const TSharedPtr<FJsonObject>* OffsetsObject;
	if (JsonObject->TryGetObjectField(TEXT("Offsets"), OffsetsObject))
	{
		ParseOffsetsData(*OffsetsObject, OutSlot.Offsets);
	}

	// 解析 Padding
	if (JsonObject->HasField(UMGPropertyKeys::Padding))
	{
		OutSlot.Padding = ParsePaddingValue(JsonObject->Values.FindRef(UMGPropertyKeys::Padding));
	}

	// 解析 HorizontalAlignment (VerticalBox/HorizontalBox/Overlay)
	FString HAlign;
	if (JsonObject->TryGetStringField(TEXT("HAlign"), HAlign) ||
		JsonObject->TryGetStringField(TEXT("HorizontalAlignment"), HAlign))
	{
		OutSlot.HorizontalAlignment = HAlign;
	}

	// 解析 VerticalAlignment
	FString VAlign;
	if (JsonObject->TryGetStringField(TEXT("VAlign"), VAlign) ||
		JsonObject->TryGetStringField(TEXT("VerticalAlignment"), VAlign))
	{
		OutSlot.VerticalAlignment = VAlign;
	}

	// 解析 SizeRule (Auto/Fill)
	FString SizeRule;
	if (JsonObject->TryGetStringField(TEXT("SizeRule"), SizeRule))
	{
		OutSlot.SizeRule = SizeRule;
	}

	// 解析 FillWeight
	double FillWeight;
	if (JsonObject->TryGetNumberField(TEXT("FillWeight"), FillWeight))
	{
		OutSlot.FillWeight = static_cast<float>(FillWeight);
	}

	// 解析 Grid 相关字段
	int32 IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Row"), IntValue)) OutSlot.Row = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Column"), IntValue)) OutSlot.Column = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("RowSpan"), IntValue)) OutSlot.RowSpan = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("ColumnSpan"), IntValue)) OutSlot.ColumnSpan = IntValue;

	// 解析 bAutoSize (Size To Content)
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
