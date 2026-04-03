// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
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

	// 瑙ｆ瀽 RootWidget
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
	if (!JsonObject->TryGetStringField(TEXT("Type"), OutNode.Type) || OutNode.Type.IsEmpty())
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

	// Parse explicit Properties payload when present.
	const TSharedPtr<FJsonObject>* PropertiesObject;
	if (JsonObject->TryGetObjectField(TEXT("Properties"), PropertiesObject))
	{
		ParseProperties(*PropertiesObject, OutNode.Properties);
	}

	// 瑙ｆ瀽 TextStyle (v2.4)
	const TSharedPtr<FJsonObject>* TSObject;
	if (JsonObject->TryGetObjectField(TEXT("TextStyle"), TSObject))
	{
		double Val;
		if ((*TSObject)->TryGetNumberField(TEXT("FontSize"), Val))
			OutNode.TextStyle.FontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(TEXT("SourceFontSize"), Val))
			OutNode.TextStyle.SourceFontSize = (float)Val;
		if ((*TSObject)->TryGetNumberField(TEXT("LetterSpacing"), Val))
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
		if ((*WidgetStyleObject)->TryGetNumberField(TEXT("WidthOverride"), Val))
		{
			OutNode.WidgetStyle.WidthOverride = (float)Val;
			OutNode.WidgetStyle.bHasWidthOverride = true;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(TEXT("HeightOverride"), Val))
		{
			OutNode.WidgetStyle.HeightOverride = (float)Val;
			OutNode.WidgetStyle.bHasHeightOverride = true;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(TEXT("MinDesiredWidth"), Val))
		{
			OutNode.WidgetStyle.MinDesiredWidth = (float)Val;
			OutNode.WidgetStyle.bHasMinDesiredWidth = true;
		}
		if ((*WidgetStyleObject)->TryGetNumberField(TEXT("MinDesiredHeight"), Val))
		{
			OutNode.WidgetStyle.MinDesiredHeight = (float)Val;
			OutNode.WidgetStyle.bHasMinDesiredHeight = true;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(TEXT("Padding")); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.Padding = ParsePaddingValue(JsonValue);
			OutNode.WidgetStyle.bHasPadding = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("InnerSlotPadding"), Str))
		{
			OutNode.WidgetStyle.InnerSlotPadding = FUMGValueParser::ParseVector2D(Str);
			OutNode.WidgetStyle.bHasInnerSlotPadding = true;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(TEXT("SlotPadding")); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.SlotPadding = ParsePaddingValue(JsonValue);
			OutNode.WidgetStyle.bHasSlotPadding = true;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(TEXT("StrokePadding")); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.StrokePadding = ParsePaddingValue(JsonValue);
			OutNode.WidgetStyle.bHasStrokePadding = true;
		}
		if (const TSharedPtr<FJsonValue> JsonValue = (*WidgetStyleObject)->TryGetField(TEXT("StrokeMargin")); JsonValue.IsValid())
		{
			OutNode.WidgetStyle.StrokeMargin = ParsePaddingValue(JsonValue);
			OutNode.WidgetStyle.bHasStrokeMargin = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("ImageMode"), Str))
		{
			OutNode.WidgetStyle.ImageMode = Str;
			OutNode.WidgetStyle.bHasImageMode = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("ImageTransform"), Str))
		{
			OutNode.WidgetStyle.ImageTransform = Str;
			OutNode.WidgetStyle.bHasImageTransform = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("Stretch"), Str))
		{
			OutNode.WidgetStyle.Stretch = Str;
			OutNode.WidgetStyle.bHasStretch = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("StretchDirection"), Str))
		{
			OutNode.WidgetStyle.StretchDirection = Str;
			OutNode.WidgetStyle.bHasStretchDirection = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("BackgroundColor"), Str))
		{
			OutNode.WidgetStyle.BackgroundColor = FUMGValueParser::ParseColor(Str);
			OutNode.WidgetStyle.bHasBackgroundColor = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("HoveredColor"), Str))
		{
			OutNode.WidgetStyle.HoveredColor = FUMGValueParser::ParseColor(Str);
			OutNode.WidgetStyle.bHasHoveredColor = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("PressedColor"), Str))
		{
			OutNode.WidgetStyle.PressedColor = FUMGValueParser::ParseColor(Str);
			OutNode.WidgetStyle.bHasPressedColor = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("NamedSlot"), Str))
		{
			OutNode.WidgetStyle.NamedSlot = Str;
			OutNode.WidgetStyle.bHasNamedSlot = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("SlotName"), Str))
		{
			OutNode.WidgetStyle.SlotName = Str;
			OutNode.WidgetStyle.bHasSlotName = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("Visibility"), Str))
		{
			OutNode.WidgetStyle.Visibility = Str;
			OutNode.WidgetStyle.bHasVisibility = true;
		}
		if ((*WidgetStyleObject)->TryGetStringField(TEXT("ToolTipText"), Str))
		{
			OutNode.WidgetStyle.ToolTipText = Str;
			OutNode.WidgetStyle.bHasToolTipText = true;
		}
		bool BoolValue = false;
		if ((*WidgetStyleObject)->TryGetBoolField(TEXT("IsEnabled"), BoolValue))
		{
			OutNode.WidgetStyle.bIsEnabled = BoolValue;
			OutNode.WidgetStyle.bHasIsEnabled = true;
		}
	}

	// Parse TextData
	const TSharedPtr<FJsonObject>* TextDataObject;
	if (JsonObject->TryGetObjectField(TEXT("TextData"), TextDataObject))
	{
		double Val = 0.0;
		FString Str;
		bool BoolValue = false;

		if ((*TextDataObject)->TryGetStringField(TEXT("Text"), Str))
		{
			OutNode.Text.Text = Str;
			OutNode.Text.bHasText = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("HintText"), Str))
		{
			OutNode.Text.HintText = Str;
			OutNode.Text.bHasHintText = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("FontFamily"), Str))
		{
			OutNode.Text.FontFamily = Str;
			OutNode.Text.bHasFontFamily = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("TypefaceFontName"), Str))
		{
			OutNode.Text.TypefaceFontName = Str;
			OutNode.Text.bHasTypefaceFontName = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("FontStyle"), Str))
		{
			OutNode.Text.FontStyle = Str;
			OutNode.Text.bHasFontStyle = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("Justification"), Str))
		{
			OutNode.Text.Justification = Str;
			OutNode.Text.bHasJustification = true;
		}
		if ((*TextDataObject)->TryGetBoolField(TEXT("AutoWrapText"), BoolValue))
		{
			OutNode.Text.bAutoWrapText = BoolValue;
			OutNode.Text.bHasAutoWrapText = true;
		}
		if ((*TextDataObject)->TryGetNumberField(TEXT("WrapTextAt"), Val))
		{
			OutNode.Text.WrapTextAt = (float)Val;
			OutNode.Text.bHasWrapTextAt = true;
		}
		if ((*TextDataObject)->TryGetNumberField(TEXT("LineHeightPercentage"), Val))
		{
			OutNode.Text.LineHeightPercentage = (float)Val;
			OutNode.Text.bHasLineHeightPercentage = true;
		}
		if ((*TextDataObject)->TryGetBoolField(TEXT("Underline"), BoolValue))
		{
			OutNode.Text.bUnderline = BoolValue;
			OutNode.Text.bHasUnderline = true;
		}
		if ((*TextDataObject)->TryGetBoolField(TEXT("Strikethrough"), BoolValue))
		{
			OutNode.Text.bStrikethrough = BoolValue;
			OutNode.Text.bHasStrikethrough = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("Href"), Str))
		{
			OutNode.Text.Href = Str;
			OutNode.Text.bHasHref = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("TextAlignVertical"), Str))
		{
			OutNode.Text.TextAlignVertical = Str;
			OutNode.Text.bHasTextAlignVertical = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("IconName"), Str))
		{
			OutNode.Text.IconName = Str;
			OutNode.Text.bHasIconName = true;
		}
		if ((*TextDataObject)->TryGetStringField(TEXT("Color"), Str))
		{
			OutNode.Text.Color = FUMGValueParser::ParseColor(Str);
			OutNode.Text.bHasColor = true;
		}
	}

	// 瑙ｆ瀽 Brush (v2.4)
	const TSharedPtr<FJsonObject>* BrushObject;
	if (JsonObject->TryGetObjectField(TEXT("Brush"), BrushObject))
	{
		double Val;
		FString Str;
		if ((*BrushObject)->TryGetNumberField(TEXT("OutlineCornerRadius"), Val))
			OutNode.Brush.OutlineCornerRadius = (float)Val;
		if ((*BrushObject)->TryGetNumberField(TEXT("OutlineThickness"), Val))
		{
			OutNode.Brush.OutlineThickness = (float)Val;
			OutNode.Brush.bHasOutlineThickness = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("DrawAs"), Str))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (TryParseJsonDrawAs(Str, DrawAs))
			{
				OutNode.Brush.DrawAs = DrawAs;
				OutNode.Brush.bHasDrawAs = true;
			}
		}
		else if ((*BrushObject)->TryGetNumberField(TEXT("DrawAs"), Val))
		{
			OutNode.Brush.DrawAs = (ESlateBrushDrawType::Type)(int32)Val;
			OutNode.Brush.bHasDrawAs = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("BrushColor"), Str))
		{
			OutNode.Brush.BrushColor = FUMGValueParser::ParseColor(Str);
			OutNode.Brush.bHasBrushColor = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("OutlineColor"), Str))
		{
			OutNode.Brush.OutlineColor = FUMGValueParser::ParseColor(Str);
			OutNode.Brush.bHasOutlineColor = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("MaterialRef"), Str))
		{
			OutNode.Brush.MaterialRef = Str;
			OutNode.Brush.bHasMaterialRef = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("MaterialParams"), Str))
		{
			OutNode.Brush.MaterialParams = Str;
			OutNode.Brush.bHasMaterialParams = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("TexturePath"), Str))
		{
			OutNode.Brush.TexturePath = Str;
			OutNode.Brush.Image.TexturePath = Str;
			OutNode.Brush.Image.bHasTexturePath = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("CornerRadii"), Str))
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
			if ((*ImageObject)->TryGetStringField(TEXT("TexturePath"), Str))
			{
				OutNode.Brush.TexturePath = Str;
				OutNode.Brush.Image.TexturePath = Str;
				OutNode.Brush.Image.bHasTexturePath = true;
			}
			if ((*ImageObject)->TryGetStringField(TEXT("Mode"), Str))
			{
				OutNode.Brush.Image.Mode = Str;
				OutNode.Brush.Image.bHasMode = true;
			}
			if ((*ImageObject)->TryGetStringField(TEXT("Transform"), Str)
				&& TryParseImageTransform(Str, OutNode.Brush.Image.Transform))
			{
				OutNode.Brush.Image.bHasTransform = true;
			}
		}
		const TSharedPtr<FJsonObject>* GradientObject = nullptr;
		if ((*BrushObject)->TryGetObjectField(TEXT("Gradient"), GradientObject))
		{
			if ((*GradientObject)->TryGetStringField(TEXT("Type"), Str))
			{
				OutNode.Brush.Gradient.Type = Str;
				OutNode.Brush.Gradient.bHasType = true;
			}
			if ((*GradientObject)->TryGetNumberField(TEXT("Rotation"), Val))
			{
				OutNode.Brush.Gradient.Rotation = (float)Val;
				OutNode.Brush.Gradient.bHasRotation = true;
			}
			if ((*GradientObject)->TryGetStringField(TEXT("Center"), Str))
			{
				OutNode.Brush.Gradient.Center = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
				OutNode.Brush.Gradient.bHasCenter = true;
			}
			if ((*GradientObject)->TryGetStringField(TEXT("Size"), Str))
			{
				OutNode.Brush.Gradient.Size = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
				OutNode.Brush.Gradient.bHasSize = true;
			}
			const TArray<TSharedPtr<FJsonValue>>* StopArray = nullptr;
			if ((*GradientObject)->TryGetArrayField(TEXT("Stops"), StopArray))
			{
				for (const TSharedPtr<FJsonValue>& StopValue : *StopArray)
				{
					const TSharedPtr<FJsonObject>* StopObject = nullptr;
					if (!StopValue->TryGetObject(StopObject) || !StopObject || !(*StopObject).IsValid())
					{
						continue;
					}

					FUMGVisualBrushData::FGradientStopData& Stop = OutNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
					if ((*StopObject)->TryGetStringField(TEXT("Color"), Str))
					{
						Stop.Color = FUMGValueParser::ParseColor(Str);
					}
					if ((*StopObject)->TryGetNumberField(TEXT("Position"), Val))
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
		if ((*EffectObject)->TryGetNumberField(TEXT("BlurStrength"), Val))
		{
			OutNode.Effect.BlurStrength = (float)Val;
			OutNode.Effect.bHasBlurStrength = true;
		}
		if ((*EffectObject)->TryGetBoolField(TEXT("RetainRendering"), BoolValue))
		{
			OutNode.Effect.bRetainRendering = BoolValue;
			OutNode.Effect.bHasRetainRendering = true;
		}
		if ((*EffectObject)->TryGetStringField(TEXT("EffectMaterial"), Str))
		{
			OutNode.Effect.EffectMaterial = Str;
			OutNode.Effect.bHasEffectMaterial = true;
		}
		if ((*EffectObject)->TryGetStringField(TEXT("TextureParameter"), Str))
		{
			OutNode.Effect.TextureParameter = Str;
			OutNode.Effect.bHasTextureParameter = true;
		}
		if ((*EffectObject)->TryGetStringField(TEXT("EffectBlurUVStep"), Str))
		{
			OutNode.Effect.EffectBlurUVStep = FUMGValueParser::ParseVector2D(Str);
			OutNode.Effect.bHasEffectBlurUVStep = true;
		}
		if ((*EffectObject)->TryGetStringField(TEXT("EffectColor"), Str))
		{
			OutNode.Effect.EffectColor = FUMGValueParser::ParseColor(Str);
			OutNode.Effect.bHasEffectColor = true;
		}
		if ((*EffectObject)->TryGetStringField(TEXT("EffectOffset"), Str))
		{
			OutNode.Effect.EffectOffset = FUMGValueParser::ParseVector2D(Str);
			OutNode.Effect.bHasEffectOffset = true;
		}
		if ((*EffectObject)->TryGetNumberField(TEXT("Phase"), Val))
		{
			OutNode.Effect.Phase = (int32)Val;
			OutNode.Effect.bHasRenderingPhase = true;
		}
		if ((*EffectObject)->TryGetNumberField(TEXT("PhaseCount"), Val))
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

		if ((*PathDataObject)->TryGetStringField(TEXT("Geometry"), Str))
		{
			OutNode.Path.Geometry = Str;
			OutNode.Path.bHasGeometry = true;
		}
		if ((*PathDataObject)->TryGetStringField(TEXT("FillRule"), Str))
		{
			OutNode.Path.FillRule = Str;
			OutNode.Path.bHasFillRule = true;
		}
		if ((*PathDataObject)->TryGetStringField(TEXT("FillColor"), Str))
		{
			OutNode.Path.FillColor = FUMGValueParser::ParseColor(Str);
			OutNode.Path.bHasFillColor = true;
		}
		if ((*PathDataObject)->TryGetStringField(TEXT("StrokeColor"), Str))
		{
			OutNode.Path.StrokeColor = FUMGValueParser::ParseColor(Str);
			OutNode.Path.bHasStrokeColor = true;
		}
		if ((*PathDataObject)->TryGetNumberField(TEXT("StrokeThickness"), Val))
		{
			OutNode.Path.StrokeThickness = (float)Val;
			OutNode.Path.bHasStrokeThickness = true;
		}
		if ((*PathDataObject)->TryGetStringField(TEXT("StrokeCap"), Str))
		{
			OutNode.Path.StrokeCap = Str;
			OutNode.Path.bHasStrokeCap = true;
		}
		if ((*PathDataObject)->TryGetStringField(TEXT("StrokeJoin"), Str))
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

		if ((*ShadowDataObject)->TryGetStringField(TEXT("ShadowOffset"), Str))
		{
			OutNode.Shadow.ShadowOffset = FUMGValueParser::ParseVector2D(Str);
			OutNode.Shadow.bHasShadowOffset = true;
		}
		if ((*ShadowDataObject)->TryGetStringField(TEXT("ShadowColorAndOpacity"), Str))
		{
			OutNode.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(Str);
			OutNode.Shadow.bHasShadowColorAndOpacity = true;
		}
		if ((*ShadowDataObject)->TryGetNumberField(TEXT("ShadowBlur"), Val))
		{
			OutNode.Shadow.ShadowBlur = (float)Val;
			OutNode.Shadow.bHasShadowBlur = true;
		}
		if ((*ShadowDataObject)->TryGetArrayField(TEXT("BoxShadows"), BoxShadowArray))
		{
			for (const TSharedPtr<FJsonValue>& LayerValue : *BoxShadowArray)
			{
				const TSharedPtr<FJsonObject>* LayerObject = nullptr;
				if (!LayerValue->TryGetObject(LayerObject) || !LayerObject || !(*LayerObject).IsValid())
				{
					continue;
				}

				FUMGBoxShadowLayerData Layer;
				if ((*LayerObject)->TryGetStringField(TEXT("Offset"), Str))
				{
					Layer.Offset = FUMGValueParser::ParseVector2D(Str);
				}
				(*LayerObject)->TryGetNumberField(TEXT("Blur"), Val);
				Layer.Blur = (float)Val;
				(*LayerObject)->TryGetNumberField(TEXT("Spread"), Val);
				Layer.Spread = (float)Val;
				if ((*LayerObject)->TryGetStringField(TEXT("Color"), Str))
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
		if ((*RenderObject)->TryGetStringField(TEXT("Clipping"), Str))
		{
			OutNode.Render.Clipping = Str;
			OutNode.Render.bHasClipping = true;
		}
		if ((*RenderObject)->TryGetNumberField(TEXT("RenderOpacity"), Val))
		{
			OutNode.Render.RenderOpacity = (float)Val;
			OutNode.Render.bHasRenderOpacity = true;
		}
		if ((*RenderObject)->TryGetNumberField(TEXT("RenderAngle"), Val))
		{
			OutNode.Render.RenderAngle = (float)Val;
			OutNode.Render.bHasRenderAngle = true;
		}
		if ((*RenderObject)->TryGetStringField(TEXT("RenderTransformPivot"), Str))
		{
			OutNode.Render.RenderTransformPivot = FUMGValueParser::ParseVector2D(Str, FVector2D(0.5f, 0.5f));
			OutNode.Render.bHasRenderTransformPivot = true;
		}
		if ((*RenderObject)->TryGetStringField(TEXT("RenderScale"), Str))
		{
			OutNode.Render.RenderScale = FUMGValueParser::ParseVector2D(Str, FVector2D(1.0f, 1.0f));
			OutNode.Render.bHasRenderScale = true;
		}
		if ((*RenderObject)->TryGetStringField(TEXT("RenderTransformScale"), Str))
		{
			OutNode.Render.RenderTransformScale = FUMGValueParser::ParseVector2D(Str);
			OutNode.Render.bHasRenderTransformScale = true;
		}
		if ((*RenderObject)->TryGetStringField(TEXT("RenderTranslation"), Str))
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
		if ((*WidgetStateObject)->TryGetNumberField(TEXT("ActiveWidgetIndex"), Val))
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

	// 瑙ｆ瀽 Offsets (CanvasPanel)
	const TSharedPtr<FJsonObject>* OffsetsObject;
	if (JsonObject->TryGetObjectField(TEXT("Offsets"), OffsetsObject))
	{
		ParseOffsetsData(*OffsetsObject, OutSlot.Offsets);
	}

	// 瑙ｆ瀽 Padding
	if (JsonObject->HasField(TEXT("Padding")))
	{
		OutSlot.Padding = ParsePaddingValue(JsonObject->Values.FindRef(TEXT("Padding")));
	}

	// 瑙ｆ瀽 HorizontalAlignment (VerticalBox/HorizontalBox/Overlay)
	FString HAlign;
	if (JsonObject->TryGetStringField(TEXT("HAlign"), HAlign) ||
		JsonObject->TryGetStringField(TEXT("HorizontalAlignment"), HAlign))
	{
		OutSlot.HorizontalAlignment = HAlign;
	}

	// 瑙ｆ瀽 VerticalAlignment
	FString VAlign;
	if (JsonObject->TryGetStringField(TEXT("VAlign"), VAlign) ||
		JsonObject->TryGetStringField(TEXT("VerticalAlignment"), VAlign))
	{
		OutSlot.VerticalAlignment = VAlign;
	}

	// 瑙ｆ瀽 SizeRule (Auto/Fill)
	FString SizeRule;
	if (JsonObject->TryGetStringField(TEXT("SizeRule"), SizeRule))
	{
		OutSlot.SizeRule = SizeRule;
	}

	// 瑙ｆ瀽 FillWeight
	double FillWeight;
	if (JsonObject->TryGetNumberField(TEXT("FillWeight"), FillWeight))
	{
		OutSlot.FillWeight = static_cast<float>(FillWeight);
	}

	// 瑙ｆ瀽 Grid 鐩稿叧瀛楁
	int32 IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Row"), IntValue)) OutSlot.Row = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("Column"), IntValue)) OutSlot.Column = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("RowSpan"), IntValue)) OutSlot.RowSpan = IntValue;
	if (JsonObject->TryGetNumberField(TEXT("ColumnSpan"), IntValue)) OutSlot.ColumnSpan = IntValue;

	// 瑙ｆ瀽 bAutoSize (Size To Content)
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

void FUMGJsonParser::ParseProperties(const TSharedPtr<FJsonObject>& JsonObject, FUMGPropertyBag& OutProperties)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	for (const auto& Pair : JsonObject->Values)
	{
		FString ValueString;
		if (Pair.Value->TryGetString(ValueString))
		{
			OutProperties.Add(Pair.Key, ValueString);
		}
		else if (Pair.Value->Type == EJson::Number)
		{
			OutProperties.Add(Pair.Key, FString::SanitizeFloat(Pair.Value->AsNumber()));
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			OutProperties.Add(Pair.Key, Pair.Value->AsBool() ? TEXT("true") : TEXT("false"));
		}
		// Serialize object/array values to JSON strings for legacy property storage.
		else if (Pair.Value->Type == EJson::Object)
		{
			FString JsonStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Pair.Value->AsObject().ToSharedRef(), Writer);
			OutProperties.Add(Pair.Key, JsonStr);
		}
		else if (Pair.Value->Type == EJson::Array)
		{
			FString JsonStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Pair.Value->AsArray(), Writer);
			OutProperties.Add(Pair.Key, JsonStr);
		}
	}
}


