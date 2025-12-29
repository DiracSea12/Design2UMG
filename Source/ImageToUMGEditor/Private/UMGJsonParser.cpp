// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGJsonParser.h"
#include "ImageToUMGLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

// ============================================================================
// 辅助函数
// ============================================================================

namespace
{
	// 解析 Padding 值（支持数字或 "L,T,R,B" 字符串格式）
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

	// 从 JSON 对象解析边界框字段
	void ParseBoundingBoxFields(const TSharedPtr<FJsonObject>& Obj, int32& X, int32& Y, int32& W, int32& H)
	{
		int32 Val;
		if (Obj->TryGetNumberField(TEXT("X"), Val)) X = Val;
		if (Obj->TryGetNumberField(TEXT("Y"), Val)) Y = Val;
		if (Obj->TryGetNumberField(TEXT("Width"), Val)) W = Val;
		if (Obj->TryGetNumberField(TEXT("Height"), Val)) H = Val;
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

	// AI 返回错误（非 UI 图片）
	FString Reason;
	if (Root->HasField(TEXT("error")))
	{
		Root->TryGetStringField(TEXT("reason"), Reason);
		if (OutError) *OutError = Reason.IsEmpty() ? TEXT("Not a UI image") : Reason;
		return false;
	}

	// 解析 RootWidget
	const TSharedPtr<FJsonObject>* RootWidget;
	if (!Root->TryGetObjectField(TEXT("RootWidget"), RootWidget) || !ParseWidgetNode(*RootWidget, OutWidgetData.RootWidget))
	{
		if (OutError) *OutError = TEXT("Invalid UI structure");
		return false;
	}

	// 空结果检测：纯容器类型但没有子节点
	// 使用精确白名单，避免误判 CheckBox/EditableTextBox 等含 "Box" 的控件
	static const TSet<FString> ContainerTypes = {
		TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"),
		TEXT("Overlay"), TEXT("ScrollBox"), TEXT("GridPanel"),
		TEXT("UniformGridPanel"), TEXT("WrapBox"), TEXT("WidgetSwitcher")
	};
	if (OutWidgetData.RootWidget.Children.Num() == 0 &&
		ContainerTypes.Contains(OutWidgetData.RootWidget.Type))
	{
		if (OutError) *OutError = TEXT("Empty UI result");
		return false;
	}

	Root->TryGetStringField(TEXT("WidgetName"), OutWidgetData.WidgetName);
	if (OutWidgetData.WidgetName.IsEmpty()) OutWidgetData.WidgetName = TEXT("WBP_Generated");

	// 解析 ImageAssets（可选）
	const TArray<TSharedPtr<FJsonValue>>* ImageAssetsArray;
	if (Root->TryGetArrayField(TEXT("ImageAssets"), ImageAssetsArray))
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : *ImageAssetsArray)
		{
			const TSharedPtr<FJsonObject>* AssetObject;
			if (AssetValue->TryGetObject(AssetObject))
			{
				FUMGImageAsset Asset;
				ParseImageAsset(*AssetObject, Asset);
				if (!Asset.Id.IsEmpty())
				{
					OutWidgetData.ImageAssets.Add(Asset);
				}
			}
		}
	}

	return true;
}

bool FUMGJsonParser::ParseWidgetNode(const TSharedPtr<FJsonObject>& JsonObject, FUMGWidgetNode& OutNode)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	// 解析必需字段 - 使用 TryGetStringField 避免字段缺失时崩溃
	if (!JsonObject->TryGetStringField(TEXT("Type"), OutNode.Type) || OutNode.Type.IsEmpty())
	{
		UE_LOG(LogImageToUMG, Error, TEXT("FUMGJsonParser: Widget node missing Type field"));
		return false;
	}

	// Name 可选，缺失时自动生成
	if (!JsonObject->TryGetStringField(TEXT("Name"), OutNode.Name) || OutNode.Name.IsEmpty())
	{
		OutNode.Name = FString::Printf(TEXT("%s_%s"), *OutNode.Type, *FGuid::NewGuid().ToString(EGuidFormats::Short));
	}

	// 解析 Slot 数据（支持 "Slot" 或 "CanvasSlot" 键名）
	const TSharedPtr<FJsonObject>* SlotObject;
	if (JsonObject->TryGetObjectField(TEXT("Slot"), SlotObject) ||
		JsonObject->TryGetObjectField(TEXT("CanvasSlot"), SlotObject))
	{
		ParseSlotData(*SlotObject, OutNode.Slot);
	}

	// 解析 Properties（支持嵌套在 "Properties" 对象或直接放在节点上）
	const TSharedPtr<FJsonObject>* PropertiesObject;
	if (JsonObject->TryGetObjectField(TEXT("Properties"), PropertiesObject))
	{
		ParseProperties(*PropertiesObject, OutNode.Properties);
	}

	// 同时解析直接放在节点上的属性（排除结构字段）
	static const TSet<FString> StructuralFields = {
		TEXT("Type"), TEXT("Name"), TEXT("Children"), TEXT("Slots"),
		TEXT("Slot"), TEXT("CanvasSlot"), TEXT("Properties")
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
			// 将嵌套对象序列化为 JSON 字符串（用于 MaterialParams 等）
			FString JsonStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Pair.Value->AsObject().ToSharedRef(), Writer);
			OutNode.Properties.Add(Pair.Key, JsonStr);
		}
	}

	// 解析 Children（标准格式）
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

	// 解析 Slots（替代格式：槽位数组，每个槽位包含 Content 和布局信息）
	const TArray<TSharedPtr<FJsonValue>>* SlotsArray;
	if (JsonObject->TryGetArrayField(TEXT("Slots"), SlotsArray))
	{
		for (const TSharedPtr<FJsonValue>& SlotValue : *SlotsArray)
		{
			const TSharedPtr<FJsonObject>* SlotItemObject;
			if (SlotValue->TryGetObject(SlotItemObject))
			{
				// 获取 Content（子控件）
				const TSharedPtr<FJsonObject>* ContentObject;
				if ((*SlotItemObject)->TryGetObjectField(TEXT("Content"), ContentObject))
				{
					FUMGWidgetNode ChildNode;
					if (ParseWidgetNode(*ContentObject, ChildNode))
					{
						// 将槽位本身的布局信息应用到子控件的 Slot
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

	// 解析 Anchors (CanvasPanel)
	const TSharedPtr<FJsonObject>* AnchorsObject;
	if (JsonObject->TryGetObjectField(TEXT("Anchors"), AnchorsObject))
	{
		ParseAnchorsData(*AnchorsObject, OutSlot.Anchors);
	}

	// 解析 Alignment (CanvasPanel) - 支持对象或数组格式
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
	if (JsonObject->HasField(TEXT("Padding")))
	{
		OutSlot.Padding = ParsePaddingValue(JsonObject->Values.FindRef(TEXT("Padding")));
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
}

void FUMGJsonParser::ParseAnchorsData(const TSharedPtr<FJsonObject>& JsonObject, FUMGAnchorsData& OutAnchors)
{
	if (!JsonObject.IsValid())
	{
		return;
	}

	// 解析 Min - 支持对象或数组格式
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

	// 解析 Max - 支持对象或数组格式
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
	}
}

void FUMGJsonParser::ParseImageAsset(const TSharedPtr<FJsonObject>& JsonObject, FUMGImageAsset& OutAsset)
{
	if (!JsonObject.IsValid()) return;

	JsonObject->TryGetStringField(TEXT("Id"), OutAsset.Id);
	JsonObject->TryGetStringField(TEXT("Type"), OutAsset.Type);
	JsonObject->TryGetStringField(TEXT("Description"), OutAsset.Description);

	// 解析边界框（支持嵌套或平铺格式）
	const TSharedPtr<FJsonObject>* BBoxObject;
	const TSharedPtr<FJsonObject>& Source = JsonObject->TryGetObjectField(TEXT("BoundingBox"), BBoxObject) ? *BBoxObject : JsonObject;
	ParseBoundingBoxFields(Source, OutAsset.X, OutAsset.Y, OutAsset.Width, OutAsset.Height);

	// 解析建议尺寸
	int32 Val;
	if (JsonObject->TryGetNumberField(TEXT("SuggestedWidth"), Val)) OutAsset.SuggestedWidth = Val;
	if (JsonObject->TryGetNumberField(TEXT("SuggestedHeight"), Val)) OutAsset.SuggestedHeight = Val;

	// 解析背景移除相关字段
	bool bRemoveBg;
	if (JsonObject->TryGetBoolField(TEXT("RemoveBg"), bRemoveBg)) OutAsset.bRemoveBg = bRemoveBg;
	JsonObject->TryGetStringField(TEXT("BgColor"), OutAsset.BgColor);
	JsonObject->TryGetStringField(TEXT("FgColor"), OutAsset.FgColor);

	// 解析背景移除高级参数
	int32 BgTolerance;
	if (JsonObject->TryGetNumberField(TEXT("BgTolerance"), BgTolerance)) OutAsset.BgTolerance = BgTolerance;
	bool bEdgeOnly;
	if (JsonObject->TryGetBoolField(TEXT("EdgeOnly"), bEdgeOnly)) OutAsset.bEdgeOnly = bEdgeOnly;
	int32 EdgeDepth;
	if (JsonObject->TryGetNumberField(TEXT("EdgeDepth"), EdgeDepth)) OutAsset.EdgeDepth = EdgeDepth;

	// 解析透明区域列表
	const TArray<TSharedPtr<FJsonValue>>* RegionsArray;
	if (JsonObject->TryGetArrayField(TEXT("TransparentRegions"), RegionsArray))
	{
		for (const TSharedPtr<FJsonValue>& RegionValue : *RegionsArray)
		{
			const TSharedPtr<FJsonObject>* RegionObj;
			if (RegionValue->TryGetObject(RegionObj))
			{
				FUMGTransparentRegion Region;
				(*RegionObj)->TryGetNumberField(TEXT("X"), Region.X);
				(*RegionObj)->TryGetNumberField(TEXT("Y"), Region.Y);
				(*RegionObj)->TryGetNumberField(TEXT("Width"), Region.Width);
				(*RegionObj)->TryGetNumberField(TEXT("Height"), Region.Height);
				if (Region.Width > 0 && Region.Height > 0)
				{
					OutAsset.TransparentRegions.Add(Region);
				}
			}
		}
	}
}
