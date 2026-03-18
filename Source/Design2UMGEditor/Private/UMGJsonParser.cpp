// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonParser.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

// ============================================================================
// 杈呭姪鍑芥暟
// ============================================================================

namespace
{
	// 瑙ｆ瀽 Padding 鍊硷紙鏀寔鏁板瓧鎴?"L,T,R,B" 瀛楃涓叉牸寮忥級
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
// 涓昏В鏋愰€昏緫
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

	// 瑙ｆ瀽 Bounds锛坴2.2鏂板锛屽綊涓€鍖栧潗鏍囷級
	const TSharedPtr<FJsonObject>* BoundsObject;
	if (JsonObject->TryGetObjectField(TEXT("Bounds"), BoundsObject))
	{
		double Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("X"), Val)) OutNode.Bounds.X = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("Y"), Val)) OutNode.Bounds.Y = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("W"), Val)) OutNode.Bounds.W = (float)Val;
		if ((*BoundsObject)->TryGetNumberField(TEXT("H"), Val)) OutNode.Bounds.H = (float)Val;
	}

	// Parse Slot data and accept the legacy CanvasSlot key.
	const TSharedPtr<FJsonObject>* SlotObject;
	if (JsonObject->TryGetObjectField(TEXT("Slot"), SlotObject) ||
		JsonObject->TryGetObjectField(TEXT("CanvasSlot"), SlotObject))
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

	// 瑙ｆ瀽 Brush (v2.4)
	const TSharedPtr<FJsonObject>* BrushObject;
	if (JsonObject->TryGetObjectField(TEXT("Brush"), BrushObject))
	{
		double Val;
		FString Str;
		if ((*BrushObject)->TryGetNumberField(TEXT("OutlineCornerRadius"), Val))
			OutNode.Brush.OutlineCornerRadius = (float)Val;
		if ((*BrushObject)->TryGetNumberField(TEXT("OutlineThickness"), Val))
			OutNode.Brush.OutlineThickness = (float)Val;
		if ((*BrushObject)->TryGetNumberField(TEXT("DrawAs"), Val))
		{
			OutNode.Brush.DrawAs = (ESlateBrushDrawType::Type)(int32)Val;
			OutNode.Brush.bHasDrawAs = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("BrushColor"), Str))
		{
			OutNode.Brush.BrushColor.InitFromString(Str);
			OutNode.Brush.bHasBrushColor = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("OutlineColor"), Str))
		{
			OutNode.Brush.OutlineColor.InitFromString(Str);
			OutNode.Brush.bHasOutlineColor = true;
		}
		if ((*BrushObject)->TryGetStringField(TEXT("MaterialRef"), Str))
			OutNode.Brush.MaterialRef = Str;
		if ((*BrushObject)->TryGetStringField(TEXT("MaterialParams"), Str))
			OutNode.Brush.MaterialParams = Str;
		if ((*BrushObject)->TryGetStringField(TEXT("TexturePath"), Str))
			OutNode.Brush.TexturePath = Str;
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
	}

	// Also capture flat properties placed directly on the node.
	static const TSet<FString> StructuralFields = {
		TEXT("Type"), TEXT("Name"), TEXT("Children"), TEXT("Slots"),
		TEXT("Slot"), TEXT("CanvasSlot"), TEXT("Properties"),
		TEXT("Id"), TEXT("Bounds"), TEXT("_meta"),  // v2.2鏂板
		TEXT("TextStyle"), TEXT("Brush")            // v2.4鏂板
	};
	for (const auto& Pair : JsonObject->Values)
	{
		if (StructuralFields.Contains(Pair.Key)) continue;

		FString ValueString;
		if (Pair.Value->TryGetString(ValueString))
		{
			OutNode.Properties.Add(Pair.Key, ValueString);
		}
		else if (Pair.Value->Type == EJson::Number)
		{
			OutNode.Properties.Add(Pair.Key, FString::SanitizeFloat(Pair.Value->AsNumber()));
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			OutNode.Properties.Add(Pair.Key, Pair.Value->AsBool() ? TEXT("true") : TEXT("false"));
		}
		else if (Pair.Value->Type == EJson::Object)
		{
			// 灏嗗祵濂楀璞″簭鍒楀寲涓?JSON 瀛楃涓诧紙鐢ㄤ簬 MaterialParams 绛夛級
			FString JsonStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Pair.Value->AsObject().ToSharedRef(), Writer);
			OutNode.Properties.Add(Pair.Key, JsonStr);
		}
	}

	// 瑙ｆ瀽 Children锛堟爣鍑嗘牸寮忥級
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

	// Parse alternative slot array format where each slot contains Content + layout info.
	const TArray<TSharedPtr<FJsonValue>>* SlotsArray;
	if (JsonObject->TryGetArrayField(TEXT("Slots"), SlotsArray))
	{
		for (const TSharedPtr<FJsonValue>& SlotValue : *SlotsArray)
		{
			const TSharedPtr<FJsonObject>* SlotItemObject;
			if (SlotValue->TryGetObject(SlotItemObject))
			{
				// Read the child widget payload from Content.
				const TSharedPtr<FJsonObject>* ContentObject;
				if ((*SlotItemObject)->TryGetObjectField(TEXT("Content"), ContentObject))
				{
					FUMGWidgetNode ChildNode;
					if (ParseWidgetNode(*ContentObject, ChildNode))
					{
						// 灏嗘Ы浣嶆湰韬殑甯冨眬淇℃伅搴旂敤鍒板瓙鎺т欢鐨?Slot
						ParseSlotData(*SlotItemObject, ChildNode.Slot);
						OutNode.Children.Add(ChildNode);
					}
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

	// 瑙ｆ瀽 Anchors (CanvasPanel)
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

void FUMGJsonParser::ParseProperties(const TSharedPtr<FJsonObject>& JsonObject, TMap<FString, FString>& OutProperties)
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


