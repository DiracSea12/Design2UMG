// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParser.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Internationalization/Regex.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FString GetStr(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Default = FString())
{
	FString Val;
	return (Obj && Obj->TryGetStringField(Key, Val)) ? Val : Default;
}

static double GetNum(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default = 0.0)
{
	double Val;
	return (Obj && Obj->TryGetNumberField(Key, Val)) ? Val : Default;
}

static bool TryGetNum(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double& OutVal)
{
	return Obj && Obj->TryGetNumberField(Key, OutVal);
}

static bool GetBool(const TSharedPtr<FJsonObject>& Obj, const FString& Key, bool Default = true)
{
	bool Val;
	return (Obj && Obj->TryGetBoolField(Key, Val)) ? Val : Default;
}

static TSharedPtr<FJsonObject> GetObj(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Obj && Obj->TryGetObjectField(Key, Sub))
		return *Sub;
	return nullptr;
}

static const TArray<TSharedPtr<FJsonValue>>* GetArr(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj && Obj->TryGetArrayField(Key, Arr))
		return Arr;
	return nullptr;
}

static FString JsonValueToCompactString(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
		return FString();

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
		return FString();
	return Output;
}

static void AddExtensionIfNonEmpty(TMap<FString, FString>& Extensions, const FString& Key, const FString& Value)
{
	if (!Value.IsEmpty())
		Extensions.Add(Key, Value);
}

static void AddNumericExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& SourceKey, const FString& ExtKey)
{
	double Val = 0.0;
	if (TryGetNum(Obj, SourceKey, Val))
		Extensions.Add(ExtKey, FString::SanitizeFloat(static_cast<float>(Val)));
}

static void AddBoolExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& Key)
{
	bool Value = false;
	if (Obj.IsValid() && Obj->TryGetBoolField(Key, Value))
		Extensions.Add(Key, Value ? TEXT("true") : TEXT("false"));
}

static void AddJsonExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& SourceKey, const FString& ExtKey)
{
	if (!Obj.IsValid())
		return;
	const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(SourceKey);
	if (!FieldValue.IsValid())
		return;
	Extensions.Add(ExtKey, JsonValueToCompactString(FieldValue));
}

static void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	if (!Obj.IsValid())
		return;

	for (const TCHAR* Key : { TEXT("overflowDirection"), TEXT("prototypeStartNodeID"), TEXT("transitionNodeID") })
		AddExtensionIfNonEmpty(Extensions, Key, GetStr(Obj, Key));

	for (const TCHAR* Key : { TEXT("layoutGrids"), TEXT("interactions"), TEXT("flowStartingPoints"), TEXT("prototypeDevice"),
		TEXT("componentPropertyReferences"), TEXT("explicitVariableModes"), TEXT("annotations"), TEXT("measurements"),
		TEXT("exportSettings"), TEXT("devStatus"), TEXT("branchData") })
	{
		AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
	}
}

static void CaptureLayoutMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	if (!Obj.IsValid())
		return;

	for (const TCHAR* Key : { TEXT("counterAxisAlignContent"), TEXT("layoutPositioning"), TEXT("layoutWrap"), TEXT("layoutAlign"),
		TEXT("primaryAxisSizingMode"), TEXT("counterAxisSizingMode"), TEXT("gridRowsSizing"), TEXT("gridColumnsSizing"),
		TEXT("gridChildHorizontalAlign"), TEXT("gridChildVerticalAlign") })
	{
		AddExtensionIfNonEmpty(Extensions, Key, GetStr(Obj, Key));
	}

	for (const TCHAR* Key : { TEXT("counterAxisSpacing"), TEXT("minWidth"), TEXT("maxWidth"), TEXT("minHeight"),
		TEXT("maxHeight"), TEXT("gridRowCount"), TEXT("gridColumnCount"), TEXT("gridRowGap"), TEXT("gridColumnGap"),
		TEXT("gridRowSpan"), TEXT("gridColumnSpan"), TEXT("gridColumnAnchorIndex"), TEXT("gridRowAnchorIndex") })
	{
		AddNumericExtensionIfPresent(Obj, Extensions, Key, Key);
	}

	for (const TCHAR* Key : { TEXT("componentProperties"), TEXT("overrides"), TEXT("exposedInstances") })
	{
		AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
	}

	CaptureSharedMetadataExtensions(Obj, Extensions);
}

static TSharedPtr<FJsonObject> ParseJsonString(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}

static bool IsVisibleSolidBlackFill(const TSharedPtr<FJsonObject>& NodeObj)
{
	const auto* Fills = GetArr(NodeObj, TEXT("fills"));
	if (!Fills || Fills->Num() == 0)
		return false;

	const TSharedPtr<FJsonObject> FillObj = (*Fills)[0]->AsObject();
	if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		return false;
	if (GetStr(FillObj, TEXT("type")) != TEXT("SOLID"))
		return false;

	const TSharedPtr<FJsonObject> ColorObj = GetObj(FillObj, TEXT("color"));
	if (!ColorObj.IsValid())
		return false;

	const double R = GetNum(ColorObj, TEXT("r"), 1.0);
	const double G = GetNum(ColorObj, TEXT("g"), 1.0);
	const double B = GetNum(ColorObj, TEXT("b"), 1.0);
	const double A = GetNum(ColorObj, TEXT("a"), 1.0);
	return R <= 0.01 && G <= 0.01 && B <= 0.01 && A >= 0.99;
}

static bool IsWrapLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	const FString Wrap = GetStr(NodeObj, TEXT("layoutWrap"));
	return Wrap.Equals(TEXT("WRAP"), ESearchCase::IgnoreCase);
}

static bool IsLikelyWrappedHorizontalLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid() || GetStr(NodeObj, TEXT("layoutMode")) != TEXT("HORIZONTAL"))
		return false;

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArr(NodeObj, TEXT("children"));
	// Threshold: 6 children minimum - typical wrapped layouts have multiple items per row
	if (!Children || Children->Num() < 6)
		return false;

	int32 FillChildren = 0;
	TArray<float> ChildRowsY;
	ChildRowsY.Reserve(Children->Num());

	for (const TSharedPtr<FJsonValue>& ChildVal : *Children)
	{
		const TSharedPtr<FJsonObject> ChildObj = ChildVal->AsObject();
		if (!ChildObj.IsValid())
			continue;

		if (GetStr(ChildObj, TEXT("layoutSizingHorizontal")) == TEXT("FILL"))
			++FillChildren;

		if (const TSharedPtr<FJsonObject> BoundsObj = GetObj(ChildObj, TEXT("absoluteBoundingBox")))
			ChildRowsY.Add(GetNum(BoundsObj, TEXT("y")));
	}

	// Threshold: 4 FILL children minimum - wrapped layouts typically have multiple fill items
	if (FillChildren < 4 || ChildRowsY.Num() < 4)
		return false;

	ChildRowsY.Sort();
	int32 RowCount = 1;
	float LastY = ChildRowsY[0];
	for (int32 Index = 1; Index < ChildRowsY.Num(); ++Index)
	{
		// Threshold: 1.0f Y deviation - items on different rows differ by at least 1 pixel vertically
		if (FMath::Abs(ChildRowsY[Index] - LastY) > 1.0f)
		{
			++RowCount;
			LastY = ChildRowsY[Index];
		}
	}

	return RowCount >= 2;
}

static bool HasSameBoundsAsParent(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	const TSharedPtr<FJsonObject> ChildBBox = GetObj(NodeObj, TEXT("absoluteBoundingBox"));
	const TSharedPtr<FJsonObject> ParentBBox = GetObj(ParentObj, TEXT("absoluteBoundingBox"));
	if (!ChildBBox.IsValid() || !ParentBBox.IsValid())
		return false;

	const double CX = GetNum(ChildBBox, TEXT("x"));
	const double CY = GetNum(ChildBBox, TEXT("y"));
	const double CW = GetNum(ChildBBox, TEXT("width"));
	const double CH = GetNum(ChildBBox, TEXT("height"));
	const double PX = GetNum(ParentBBox, TEXT("x"));
	const double PY = GetNum(ParentBBox, TEXT("y"));
	const double PW = GetNum(ParentBBox, TEXT("width"));
	const double PH = GetNum(ParentBBox, TEXT("height"));
	return FMath::IsNearlyEqual(CX, PX, 0.5) &&
		FMath::IsNearlyEqual(CY, PY, 0.5) &&
		FMath::IsNearlyEqual(CW, PW, 0.5) &&
		FMath::IsNearlyEqual(CH, PH, 0.5);
}

static FString NormalizeWindingRule(const FString& InRule)
{
	if (InRule.Equals(TEXT("EVENODD"), ESearchCase::IgnoreCase))
		return TEXT("evenodd");
	return TEXT("nonzero");
}

static bool ParseGeometryArray(
	const TArray<TSharedPtr<FJsonValue>>* GeometryArr,
	FString& OutPath,
	FString& OutRule)
{
	if (!GeometryArr)
		return false;

	for (const TSharedPtr<FJsonValue>& GeometryVal : *GeometryArr)
	{
		const TSharedPtr<FJsonObject> GeometryObj = GeometryVal->AsObject();
		if (!GeometryObj.IsValid())
			continue;

		const FString Path = GetStr(GeometryObj, TEXT("path"));
		if (Path.IsEmpty())
			continue;

		OutPath = Path;
		OutRule = GetStr(GeometryObj, TEXT("windingRule"));
		return true;
	}

	return false;
}

static bool IsSemanticFieldName(const FString& FieldName)
{
	return FieldName.Equals(TEXT("placeholder"), ESearchCase::IgnoreCase)
		|| FieldName.Equals(TEXT("slot"), ESearchCase::IgnoreCase);
}

static bool IsExplicitFalseString(const FString& InValue)
{
	const FString V = InValue.TrimStartAndEnd().ToLower();
	return V.IsEmpty()
		|| V == TEXT("false")
		|| V == TEXT("0")
		|| V == TEXT("no")
		|| V == TEXT("off")
		|| V == TEXT("none")
		|| V == TEXT("null");
}

static bool ReadSemanticSignalField(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	switch (Value->Type)
	{
	case EJson::Boolean:
		return Value->AsBool();
	case EJson::Number:
		return !FMath::IsNearlyZero(static_cast<float>(Value->AsNumber()));
	case EJson::String:
		return !IsExplicitFalseString(Value->AsString());
	case EJson::Array:
		// Keep behavior aligned with PenParser: slot/placeholder arrays signal semantic intent,
		// even if currently empty.
		return true;
	case EJson::Object:
		return true;
	default:
		return false;
	}
}

static bool TryReadSemanticHintFromJsonValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Obj->Values)
		{
			if (IsSemanticFieldName(Pair.Key) && ReadSemanticSignalField(Pair.Value))
			{
				return true;
			}
			if (TryReadSemanticHintFromJsonValue(Pair.Value))
			{
				return true;
			}
		}
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Child : Value->AsArray())
		{
			if (TryReadSemanticHintFromJsonValue(Child))
			{
				return true;
			}
		}
		return false;
	}

	if (Value->Type == EJson::String)
	{
		const FString Str = Value->AsString().TrimStartAndEnd();
		if (Str.IsEmpty())
		{
			return false;
		}

		// pluginData 常见为 JSON 字符串，优先按 JSON 解。
		TSharedPtr<FJsonObject> JsonObj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			return TryReadSemanticHintFromJsonValue(MakeShared<FJsonValueObject>(JsonObj));
		}

		return IsSemanticFieldName(Str);
	}

	return false;
}

static void ApplyFigmaSemanticHints(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node)
{
	const TSharedPtr<FJsonValue> PlaceholderField = NodeObj->TryGetField(TEXT("placeholder"));
	const TSharedPtr<FJsonValue> SlotField = NodeObj->TryGetField(TEXT("slot"));

	if (SlotField.IsValid() && SlotField->Type == EJson::Array)
	{
		const int32 SlotCount = SlotField->AsArray().Num();
		Node.Metadata.Add(TEXT("figma.slot.present"), TEXT("true"));
		Node.Metadata.Add(TEXT("figma.slot.count"), FString::FromInt(SlotCount));
		if (SlotCount == 0)
			Node.Metadata.Add(TEXT("figma.slot.empty"), TEXT("true"));
	}

	const bool bExplicitPlaceholder = ReadSemanticSignalField(PlaceholderField);
	const bool bExplicitSlot = ReadSemanticSignalField(SlotField);

	const bool bHasPluginSemanticMarker =
		TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("pluginData")))
		|| TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("sharedPluginData")));

	Node.bSemanticPlaceholderSurface =
		bExplicitPlaceholder
		|| bExplicitSlot
		|| bHasPluginSemanticMarker;
}

struct FNodeBounds
{
	float X = 0.0f;
	float Y = 0.0f;
	float W = 0.0f;
	float H = 0.0f;
};

static bool TryReadBounds(const TSharedPtr<FJsonObject>& BoundsObj, FNodeBounds& OutBounds)
{
	if (!BoundsObj.IsValid())
	{
		return false;
	}

	OutBounds.X = GetNum(BoundsObj, TEXT("x"));
	OutBounds.Y = GetNum(BoundsObj, TEXT("y"));
	OutBounds.W = GetNum(BoundsObj, TEXT("width"));
	OutBounds.H = GetNum(BoundsObj, TEXT("height"));
	return OutBounds.W > 0.0f && OutBounds.H > 0.0f;
}

static bool HasVisibleStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArr(NodeObj, TEXT("strokes"));
	if (!Strokes || Strokes->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& StrokeVal : *Strokes)
	{
		const TSharedPtr<FJsonObject> StrokeObj = StrokeVal->AsObject();
		if (StrokeObj.IsValid() && GetBool(StrokeObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

static void GetStrokeExpansion(const TSharedPtr<FJsonObject>& NodeObj, float& OutLeft, float& OutRight, float& OutTop, float& OutBottom)
{
	OutLeft = 0.0f;
	OutRight = 0.0f;
	OutTop = 0.0f;
	OutBottom = 0.0f;

	if (!HasVisibleStroke(NodeObj))
	{
		return;
	}

	float Left = 0.0f;
	float Right = 0.0f;
	float Top = 0.0f;
	float Bottom = 0.0f;
	const TSharedPtr<FJsonObject> IndividualWeights = GetObj(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights.IsValid())
	{
		Top = GetNum(IndividualWeights, TEXT("top"));
		Right = GetNum(IndividualWeights, TEXT("right"));
		Bottom = GetNum(IndividualWeights, TEXT("bottom"));
		Left = GetNum(IndividualWeights, TEXT("left"));
	}
	else
	{
		const float Uniform = GetNum(NodeObj, TEXT("strokeWeight"));
		Top = Uniform;
		Right = Uniform;
		Bottom = Uniform;
		Left = Uniform;
	}

	const FString Align = GetStr(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
	{
		OutLeft = Left;
		OutRight = Right;
		OutTop = Top;
		OutBottom = Bottom;
	}
	else if (Align == TEXT("CENTER"))
	{
		OutLeft = Left * 0.5f;
		OutRight = Right * 0.5f;
		OutTop = Top * 0.5f;
		OutBottom = Bottom * 0.5f;
	}
}

static bool TryGetVisualBounds(const TSharedPtr<FJsonObject>& NodeObj, FNodeBounds& OutBounds)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	// Prefer Figma visual bounds when present (includes stroke/effects).
	if (TryReadBounds(GetObj(NodeObj, TEXT("absoluteRenderBounds")), OutBounds))
	{
		return true;
	}

	if (!TryReadBounds(GetObj(NodeObj, TEXT("absoluteBoundingBox")), OutBounds))
	{
		return false;
	}

	// Local export may omit absoluteRenderBounds; expand bbox by stroke align as fallback.
	float ExpandLeft = 0.0f;
	float ExpandRight = 0.0f;
	float ExpandTop = 0.0f;
	float ExpandBottom = 0.0f;
	GetStrokeExpansion(NodeObj, ExpandLeft, ExpandRight, ExpandTop, ExpandBottom);

	OutBounds.X -= ExpandLeft;
	OutBounds.Y -= ExpandTop;
	OutBounds.W += ExpandLeft + ExpandRight;
	OutBounds.H += ExpandTop + ExpandBottom;
	return OutBounds.W > 0.0f && OutBounds.H > 0.0f;
}

static void ParsePathGeometry(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	FString GeometryPath;
	FString WindingRule;

	// Prefer fillGeometry (visual fill shape). Fallback to strokeGeometry for stroke-only vectors/lines.
	if (!ParseGeometryArray(GetArr(NodeObj, TEXT("fillGeometry")), GeometryPath, WindingRule))
	{
		ParseGeometryArray(GetArr(NodeObj, TEXT("strokeGeometry")), GeometryPath, WindingRule);
	}

	if (!GeometryPath.IsEmpty())
	{
		OutNode.Geometry = GeometryPath;
		if (!WindingRule.IsEmpty())
		{
			OutNode.FillRule = NormalizeWindingRule(WindingRule);
		}
	}
}

// ---------------------------------------------------------------------------
// MapNodeType
// ---------------------------------------------------------------------------

EDesignNodeType FFigmaParser::MapNodeType(const FString& FigmaType)
{
	if (FigmaType == TEXT("FRAME") || FigmaType == TEXT("COMPONENT_SET") || FigmaType == TEXT("SECTION"))
		return EDesignNodeType::Frame;
	if (FigmaType == TEXT("COMPONENT"))
		return EDesignNodeType::Frame;
	if (FigmaType == TEXT("INSTANCE"))
		return EDesignNodeType::Frame;  // Figma API已展开INSTANCE children，直接当Frame处理
	if (FigmaType == TEXT("GROUP"))
		return EDesignNodeType::Group;
	if (FigmaType == TEXT("RECTANGLE"))
		return EDesignNodeType::Rectangle;
	if (FigmaType == TEXT("ELLIPSE"))
		return EDesignNodeType::Ellipse;
	if (FigmaType == TEXT("LINE"))
		return EDesignNodeType::Line;
	if (FigmaType == TEXT("REGULAR_POLYGON") || FigmaType == TEXT("STAR"))
		return EDesignNodeType::Polygon;
	if (FigmaType == TEXT("VECTOR") || FigmaType == TEXT("BOOLEAN_OPERATION"))
		return EDesignNodeType::Path;
	if (FigmaType == TEXT("TEXT"))
		return EDesignNodeType::Text;
	return EDesignNodeType::Unknown;
}

// ---------------------------------------------------------------------------
// HasAutoLayout
// ---------------------------------------------------------------------------

bool FFigmaParser::HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (IsWrapLayout(NodeObj) || IsLikelyWrappedHorizontalLayout(NodeObj))
		return true;

	FString Mode = GetStr(NodeObj, TEXT("layoutMode"));
	return Mode == TEXT("HORIZONTAL") || Mode == TEXT("VERTICAL") || Mode == TEXT("GRID");
}

// ---------------------------------------------------------------------------
// ColorToHex
// ---------------------------------------------------------------------------

FString FFigmaParser::ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity)
{
	if (!ColorObj) return TEXT("#000000");

	uint8 R = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("r")) * 255.0), 0, 255);
	uint8 G = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("g")) * 255.0), 0, 255);
	uint8 B = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("b")) * 255.0), 0, 255);
	float A = GetNum(ColorObj, TEXT("a"), 1.0) * Opacity;
	uint8 AI = FMath::Clamp(FMath::RoundToInt(A * 255.0), 0, 255);

	if (AI >= 255)
		return FString::Printf(TEXT("#%02X%02X%02X"), R, G, B);
	return FString::Printf(TEXT("#%02X%02X%02X%02X"), R, G, B, AI);
}

// ---------------------------------------------------------------------------
// ParseLayout
// ---------------------------------------------------------------------------

void FFigmaParser::ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	const bool bWrapLayout = IsWrapLayout(NodeObj) || IsLikelyWrappedHorizontalLayout(NodeObj);
	FString Mode = GetStr(NodeObj, TEXT("layoutMode"));
	if (bWrapLayout)
	{
		OutNode.Layout = EDesignLayout::Wrap;
	}
	else if (Mode == TEXT("HORIZONTAL"))
	{
		OutNode.Layout = EDesignLayout::Horizontal;
	}
	else if (Mode == TEXT("VERTICAL"))
	{
		OutNode.Layout = EDesignLayout::Vertical;
	}
	else if (Mode == TEXT("GRID"))
	{
		OutNode.Layout = EDesignLayout::Grid;
	}
	else
	{
		OutNode.Layout = EDesignLayout::None;
	}

	OutNode.Gap = GetNum(NodeObj, TEXT("itemSpacing"));

	OutNode.Padding.Left = GetNum(NodeObj, TEXT("paddingLeft"));
	OutNode.Padding.Right = GetNum(NodeObj, TEXT("paddingRight"));
	OutNode.Padding.Top = GetNum(NodeObj, TEXT("paddingTop"));
	OutNode.Padding.Bottom = GetNum(NodeObj, TEXT("paddingBottom"));
	OutNode.bAbsoluteLayoutPositioning = GetStr(NodeObj, TEXT("layoutPositioning")).Equals(TEXT("ABSOLUTE"), ESearchCase::IgnoreCase);

	{
		CaptureLayoutMetadataExtensions(NodeObj, OutNode.Metadata);
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("itemReverseZIndex"));
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("isExposedInstance"));
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("strokesIncludedInLayout"));

		const TSharedPtr<FJsonValue> BoundVarsField = NodeObj->TryGetField(TEXT("boundVariables"));
		if (BoundVarsField.IsValid() && BoundVarsField->Type == EJson::Object)
		{
			OutNode.Metadata.Add(TEXT("boundVariables"), JsonValueToCompactString(BoundVarsField));
			const TSharedPtr<FJsonObject> BoundVarsObj = BoundVarsField->AsObject();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BoundVarsObj->Values)
			{
				OutNode.Metadata.Add(FString::Printf(TEXT("boundVariables.%s"), *Pair.Key), JsonValueToCompactString(Pair.Value));
			}
		}
	}

	{
		FString Primary = GetStr(NodeObj, TEXT("primaryAxisAlignItems"));
		if (Primary == TEXT("CENTER"))
			OutNode.JustifyContent = EDesignJustifyContent::Center;
		else if (Primary == TEXT("MAX"))
			OutNode.JustifyContent = EDesignJustifyContent::End;
		else if (Primary == TEXT("SPACE_BETWEEN"))
			OutNode.JustifyContent = EDesignJustifyContent::SpaceBetween;
		else
			OutNode.JustifyContent = EDesignJustifyContent::Start;

		FString Counter = GetStr(NodeObj, TEXT("counterAxisAlignItems"));
		if (Counter == TEXT("CENTER"))
			OutNode.AlignItems = EDesignAlignItems::Center;
		else if (Counter == TEXT("MAX"))
			OutNode.AlignItems = EDesignAlignItems::End;
		else
			OutNode.AlignItems = EDesignAlignItems::Start;
	}

	OutNode.bHasClip = true;
	OutNode.bClip = GetBool(NodeObj, TEXT("clipsContent"), false);
}

// ---------------------------------------------------------------------------
// ParseSize
// ---------------------------------------------------------------------------

void FFigmaParser::ParseSize(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	FNodeBounds VisualBounds;
	FNodeBounds LayoutBounds;
	const bool bHasVisualBounds = TryGetVisualBounds(NodeObj, VisualBounds);
	const bool bHasLayoutBounds = TryReadBounds(GetObj(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

	// Auto layout should remain in layout space; absolute layout uses visual bounds.
	const FNodeBounds& EffectiveBounds = bParentHasAutoLayout
		? (bHasLayoutBounds ? LayoutBounds : VisualBounds)
		: (bHasVisualBounds ? VisualBounds : LayoutBounds);

	float W = EffectiveBounds.W;
	float H = EffectiveBounds.H;

	if (bParentHasAutoLayout)
	{
		FString SizingH = GetStr(NodeObj, TEXT("layoutSizingHorizontal"), TEXT("FIXED"));
		if (SizingH == TEXT("FILL"))
			OutNode.Width = FDesignSize::Fill(W);
		else if (SizingH == TEXT("HUG"))
			OutNode.Width = FDesignSize::Fit(W);
		else
			OutNode.Width = FDesignSize::Fixed(W);

		FString SizingV = GetStr(NodeObj, TEXT("layoutSizingVertical"), TEXT("FIXED"));
		if (SizingV == TEXT("FILL"))
			OutNode.Height = FDesignSize::Fill(H);
		else if (SizingV == TEXT("HUG"))
			OutNode.Height = FDesignSize::Fit(H);
		else
			OutNode.Height = FDesignSize::Fixed(H);
	}
	else
	{
		OutNode.Width = FDesignSize::Fixed(W);
		OutNode.Height = FDesignSize::Fixed(H);
	}

	// Figma layoutGrow: 1.0 = fill, 0.0 = fixed (only meaningful in auto-layout)
	double GrowVal = 0.0;
	if (NodeObj->TryGetNumberField(TEXT("layoutGrow"), GrowVal))
		OutNode.LayoutGrow = static_cast<float>(GrowVal);

	OutNode.bHasWidth = true;
	OutNode.bHasHeight = true;
}

// ---------------------------------------------------------------------------
// ParseSingleFill - 解析单个 Figma fill 对象
// ---------------------------------------------------------------------------

FDesignFill FFigmaParser::ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj)
{
	FDesignFill Result;
	if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
		return Result;

	FString FillType = GetStr(FillObj, TEXT("type"));
	float FillOpacity = GetNum(FillObj, TEXT("opacity"), 1.0);
	Result.Opacity = FillOpacity;
	FillObj->TryGetStringField(TEXT("blendMode"), Result.BlendMode);

	if (FillType == TEXT("SOLID"))
	{
		auto ColorObj = GetObj(FillObj, TEXT("color"));
		Result.Type = EDesignFillType::Color;
		Result.SolidColor = ColorToHex(ColorObj, FillOpacity);
		return Result;
	}

	if (FillType == TEXT("GRADIENT_LINEAR") || FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_ANGULAR") || FillType == TEXT("GRADIENT_DIAMOND"))
	{
		Result.Type = EDesignFillType::Gradient;

		if (FillType == TEXT("GRADIENT_LINEAR"))
			Result.Gradient.GradientType = EDesignGradientType::Linear;
		else if (FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_DIAMOND"))
			Result.Gradient.GradientType = EDesignGradientType::Radial;
		else
			Result.Gradient.GradientType = EDesignGradientType::Angular;

		const auto* Handles = GetArr(FillObj, TEXT("gradientHandlePositions"));
		if (Handles && Handles->Num() >= 2)
		{
			auto P0 = (*Handles)[0]->AsObject();
			auto P1 = (*Handles)[1]->AsObject();
			if (P0 && P1)
			{
				float DX = GetNum(P1, TEXT("x")) - GetNum(P0, TEXT("x"));
				float DY = GetNum(P1, TEXT("y")) - GetNum(P0, TEXT("y"));
				Result.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
			}
		}

		const auto* Stops = GetArr(FillObj, TEXT("gradientStops"));
		if (Stops)
		{
			for (const auto& StopVal : *Stops)
			{
				auto StopObj = StopVal->AsObject();
				if (!StopObj) continue;

				FDesignGradientStop Stop;
				Stop.Color = ColorToHex(GetObj(StopObj, TEXT("color")), FillOpacity);
				Stop.Position = GetNum(StopObj, TEXT("position"));
				Result.Gradient.Colors.Add(Stop);
			}
		}
		return Result;
	}

	if (FillType == TEXT("IMAGE"))
	{
		Result.Type = EDesignFillType::Image;
		Result.Image.Url = GetStr(FillObj, TEXT("imageRef"));
		Result.Image.Opacity = FillOpacity;

		FString ScaleMode = GetStr(FillObj, TEXT("scaleMode"));
		if (ScaleMode == TEXT("FILL"))
			Result.Image.Mode = EDesignImageMode::Fill;
		else if (ScaleMode == TEXT("FIT"))
			Result.Image.Mode = EDesignImageMode::Fit;
		else if (ScaleMode == TEXT("CROP"))
			Result.Image.Mode = EDesignImageMode::Crop;
		else
			Result.Image.Mode = EDesignImageMode::Stretch;

		// 解析 imageTransform（2x3 仿射矩阵 [[a,b,tx],[c,d,ty]]）
		const auto* TransformArr = GetArr(FillObj, TEXT("imageTransform"));
		if (TransformArr && TransformArr->Num() == 2)
		{
			auto Row0 = (*TransformArr)[0]->AsArray();
			auto Row1 = (*TransformArr)[1]->AsArray();
			if (Row0.Num() == 3 && Row1.Num() == 3)
			{
				Result.Image.Transform.A  = Row0[0]->AsNumber();
				Result.Image.Transform.B  = Row0[1]->AsNumber();
				Result.Image.Transform.TX = Row0[2]->AsNumber();
				Result.Image.Transform.C  = Row1[0]->AsNumber();
				Result.Image.Transform.D  = Row1[1]->AsNumber();
				Result.Image.Transform.TY = Row1[2]->AsNumber();
			}
		}

		return Result;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseFills - 收集所有可见填充层（底→顶顺序）
// ---------------------------------------------------------------------------

TArray<FDesignFill> FFigmaParser::ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	TArray<FDesignFill> Result;
	for (int32 i = 0; i < Fills.Num(); ++i)
	{
		auto FillObj = Fills[i]->AsObject();
		if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
			continue;

		FDesignFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EDesignFillType::None)
			Result.Add(MoveTemp(Fill));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// ParseFill - 返回最顶层可见填充（向后兼容）
// ---------------------------------------------------------------------------

FDesignFill FFigmaParser::ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	// Take the last visible fill (Figma renders last on top)
	for (int32 i = Fills.Num() - 1; i >= 0; --i)
	{
		auto FillObj = Fills[i]->AsObject();
		if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
			continue;

		FDesignFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EDesignFillType::None)
			return Fill;
	}
	return FDesignFill();
}

// ---------------------------------------------------------------------------
// ParseStroke
// ---------------------------------------------------------------------------

FDesignStroke FFigmaParser::ParseStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	FDesignStroke Result;
	bool bHasVisibleStroke = false;

	const auto* Strokes = GetArr(NodeObj, TEXT("strokes"));
	if (Strokes)
	{
		for (const auto& StrokeVal : *Strokes)
		{
			auto StrokeObj = StrokeVal->AsObject();
			if (!StrokeObj || !GetBool(StrokeObj, TEXT("visible"), true))
				continue;

			bHasVisibleStroke = true;
			auto ColorObj = GetObj(StrokeObj, TEXT("color"));
			float StrokeOpacity = GetNum(StrokeObj, TEXT("opacity"), 1.0);
			Result.Fill = FDesignFill::FromColor(ColorToHex(ColorObj, StrokeOpacity));
			break;
		}
	}

	// Figma nodes often carry default strokeWeight fields even when no stroke is enabled.
	// Only map thickness/alignment when at least one visible stroke exists.
	if (!bHasVisibleStroke)
	{
		// Explicitly clear thickness to avoid confusion from default strokeWeight fields
		Result.Thickness = FDesignStrokeThickness();
		return Result;
	}

	// Stroke align
	FString Align = GetStr(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
		Result.Align = EDesignStrokeAlign::Outside;
	else if (Align == TEXT("CENTER"))
		Result.Align = EDesignStrokeAlign::Center;
	else
		Result.Align = EDesignStrokeAlign::Inside;

	// Individual stroke weights
	auto IndividualWeights = GetObj(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights)
	{
		Result.Thickness.bUniform = false;
		Result.Thickness.Top = GetNum(IndividualWeights, TEXT("top"));
		Result.Thickness.Right = GetNum(IndividualWeights, TEXT("right"));
		Result.Thickness.Bottom = GetNum(IndividualWeights, TEXT("bottom"));
		Result.Thickness.Left = GetNum(IndividualWeights, TEXT("left"));
	}
	else
	{
		Result.Thickness.bUniform = true;
		Result.Thickness.Uniform = GetNum(NodeObj, TEXT("strokeWeight"));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseCornerRadius
// ---------------------------------------------------------------------------

FDesignCornerRadius FFigmaParser::ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj)
{
	FDesignCornerRadius Result;

	const auto* Radii = GetArr(NodeObj, TEXT("rectangleCornerRadii"));
	if (Radii && Radii->Num() == 4)
	{
		const float TopLeft = (*Radii)[0]->AsNumber();
		const float TopRight = (*Radii)[1]->AsNumber();
		const float BottomRight = (*Radii)[2]->AsNumber();
		const float BottomLeft = (*Radii)[3]->AsNumber();
		const bool bAllEqual =
			FMath::IsNearlyEqual(TopLeft, TopRight, KINDA_SMALL_NUMBER) &&
			FMath::IsNearlyEqual(TopLeft, BottomRight, KINDA_SMALL_NUMBER) &&
			FMath::IsNearlyEqual(TopLeft, BottomLeft, KINDA_SMALL_NUMBER);

		if (bAllEqual)
		{
			Result.bUniform = true;
			Result.Uniform = TopLeft;
		}
		else
		{
			Result.bUniform = false;
			Result.TopLeft = TopLeft;
			Result.TopRight = TopRight;
			Result.BottomRight = BottomRight;
			Result.BottomLeft = BottomLeft;
		}
	}
	else
	{
		Result.bUniform = true;
		Result.Uniform = GetNum(NodeObj, TEXT("cornerRadius"));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseEffects
// ---------------------------------------------------------------------------

TArray<FDesignEffect> FFigmaParser::ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects)
{
	TArray<FDesignEffect> Result;

	for (const auto& EffVal : Effects)
	{
		auto EffObj = EffVal->AsObject();
		if (!EffObj || !GetBool(EffObj, TEXT("visible"), true))
			continue;

		FString EffType = GetStr(EffObj, TEXT("type"));
		FDesignEffect Eff;
		EffObj->TryGetStringField(TEXT("blendMode"), Eff.BlendMode);

		if (EffType == TEXT("DROP_SHADOW") || EffType == TEXT("INNER_SHADOW"))
		{
			Eff.Type = EDesignEffectType::Shadow;
			Eff.Shadow.bInner = (EffType == TEXT("INNER_SHADOW"));
			Eff.Shadow.BlendMode = Eff.BlendMode;

			auto Offset = GetObj(EffObj, TEXT("offset"));
			Eff.Shadow.OffsetX = Offset ? GetNum(Offset, TEXT("x")) : 0.0f;
			Eff.Shadow.OffsetY = Offset ? GetNum(Offset, TEXT("y")) : 0.0f;
			Eff.Shadow.Blur = GetNum(EffObj, TEXT("radius"));
			Eff.Shadow.Spread = GetNum(EffObj, TEXT("spread"));
			float ShadowOpacity = GetNum(EffObj, TEXT("opacity"), 1.0);
			Eff.Shadow.Color = ColorToHex(GetObj(EffObj, TEXT("color")), ShadowOpacity);
		}
		else if (EffType == TEXT("LAYER_BLUR"))
		{
			Eff.Type = EDesignEffectType::Blur;
			Eff.Radius = GetNum(EffObj, TEXT("radius"));
		}
		else if (EffType == TEXT("BACKGROUND_BLUR"))
		{
			Eff.Type = EDesignEffectType::BackgroundBlur;
			Eff.Radius = GetNum(EffObj, TEXT("radius"));
		}
		else
		{
			continue;
		}

		Result.Add(Eff);
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseTextProperties
// ---------------------------------------------------------------------------

void FFigmaParser::ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	OutNode.Content = GetStr(NodeObj, TEXT("characters"));

	auto Style = GetObj(NodeObj, TEXT("style"));
	if (!Style) return;

	OutNode.FontFamily = GetStr(Style, TEXT("fontFamily"));
	OutNode.FontSize = GetNum(Style, TEXT("fontSize"), 16.0);
	OutNode.FontStyle = GetStr(Style, TEXT("fontStyle"), TEXT("normal"));

	int32 Weight = FMath::RoundToInt(GetNum(Style, TEXT("fontWeight"), 400.0));
	// Snap to nearest standard weight (100-900)
	Weight = FMath::Clamp(FMath::RoundToInt(Weight / 100.0f) * 100, 100, 900);
	OutNode.FontWeight = FString::FromInt(Weight);

	FString LineHeightUnit = GetStr(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
	{
		// UMG TextBlock::LineHeightPercentage expects a ratio (e.g. 1.2),
		// while Figma provides px or percent-based values.
		if (LineHeightUnit == TEXT("PIXELS"))
		{
			const float LineHeightPx = GetNum(Style, TEXT("lineHeightPx"));
			if (LineHeightPx > 0.0f && OutNode.FontSize > 0.0f)
				OutNode.LineHeight = LineHeightPx / OutNode.FontSize;
		}
		else if (LineHeightUnit == TEXT("FONT_SIZE_%"))
		{
			const float Percent = GetNum(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.LineHeight = Percent / 100.0f;
		}
		else
		{
			// Fallback for less common payloads.
			const float Percent = GetNum(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.LineHeight = Percent / 100.0f;
		}
	}

	OutNode.LetterSpacing = GetNum(Style, TEXT("letterSpacing"));
	FString TextDecoration = GetStr(Style, TEXT("textDecoration"));
	OutNode.bUnderline = (TextDecoration == TEXT("UNDERLINE"));
	OutNode.bStrikethrough = (TextDecoration == TEXT("STRIKETHROUGH"));
	if (const TSharedPtr<FJsonObject> HyperlinkObj = GetObj(NodeObj, TEXT("hyperlink")))
	{
		OutNode.Href = GetStr(HyperlinkObj, TEXT("url"));
	}

	FString HAlign = GetStr(Style, TEXT("textAlignHorizontal"), TEXT("LEFT"));
	if (HAlign == TEXT("CENTER"))
		OutNode.TextAlign = EDesignTextAlign::Center;
	else if (HAlign == TEXT("RIGHT"))
		OutNode.TextAlign = EDesignTextAlign::Right;
	else if (HAlign == TEXT("JUSTIFIED"))
		OutNode.TextAlign = EDesignTextAlign::Justify;
	else
		OutNode.TextAlign = EDesignTextAlign::Left;

	FString VAlign = GetStr(Style, TEXT("textAlignVertical"), TEXT("TOP"));
	if (VAlign == TEXT("CENTER"))
		OutNode.TextAlignVertical = EDesignTextAlignVertical::Middle;
	else if (VAlign == TEXT("BOTTOM"))
		OutNode.TextAlignVertical = EDesignTextAlignVertical::Bottom;
	else
		OutNode.TextAlignVertical = EDesignTextAlignVertical::Top;

	// textAutoResize can appear on node root or inside style (nodes API payload).
	FString AutoResize = GetStr(NodeObj, TEXT("textAutoResize"));
	if (AutoResize.IsEmpty())
	{
		AutoResize = GetStr(Style, TEXT("textAutoResize"), TEXT("NONE"));
	}
	if (AutoResize == TEXT("WIDTH_AND_HEIGHT"))
		OutNode.TextGrowth = EDesignTextGrowth::Auto;
	else if (AutoResize == TEXT("HEIGHT"))
		OutNode.TextGrowth = EDesignTextGrowth::FixedWidth;
	else
		OutNode.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
}

// ---------------------------------------------------------------------------
// ParsePosition
// ---------------------------------------------------------------------------

void FFigmaParser::ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.bHasX = false;
		OutNode.bHasY = false;
		return;
	}

	FNodeBounds ChildBounds;
	const bool bHasChildBounds = TryGetVisualBounds(NodeObj, ChildBounds);

	FNodeBounds ParentBounds;
	const bool bHasParentBounds = ParentObj && TryGetVisualBounds(ParentObj, ParentBounds);

	if (bHasChildBounds && bHasParentBounds)
	{
		OutNode.X = ChildBounds.X - ParentBounds.X;
		OutNode.Y = ChildBounds.Y - ParentBounds.Y;
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
	else if (bHasChildBounds)
	{
		OutNode.X = ChildBounds.X;
		OutNode.Y = ChildBounds.Y;
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
}

// ---------------------------------------------------------------------------
// ParseNode (recursive)
// ---------------------------------------------------------------------------

TSharedPtr<FDesignNode> FFigmaParser::ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj) return nullptr;

	FString FigmaType = GetStr(NodeObj, TEXT("type"));
	if (FigmaType.IsEmpty()) return nullptr;

	// Figma mask 节点是裁剪几何，不应直接生成可视控件。
	const bool bIsMaskNode = GetBool(NodeObj, TEXT("isMask"), false) || !GetStr(NodeObj, TEXT("maskType")).IsEmpty();
	if (bIsMaskNode)
	{
		return nullptr;
	}

	// 本地导出数据可能丢失 isMask 字段：对典型“黑色同尺寸遮罩矩形”做保守兜底。
	if (ParentObj &&
		FigmaType == TEXT("RECTANGLE") &&
		GetStr(NodeObj, TEXT("name")) == TEXT("Rectangle") &&
		IsVisibleSolidBlackFill(NodeObj) &&
		HasSameBoundsAsParent(NodeObj, ParentObj))
	{
		const bool bParentClips = GetBool(ParentObj, TEXT("clipsContent"), false);
		const bool bParentGroup = (GetStr(ParentObj, TEXT("type")) == TEXT("GROUP"));
		if (bParentClips || bParentGroup)
		{
			return nullptr;
		}
	}

	auto Node = MakeShared<FDesignNode>();
	Node->Id = GetStr(NodeObj, TEXT("id"));
	Node->Name = GetStr(NodeObj, TEXT("name"));
	Node->bEnabled = GetBool(NodeObj, TEXT("visible"), true);
	Node->Opacity = GetNum(NodeObj, TEXT("opacity"), 1.0);
	Node->Rotation = GetNum(NodeObj, TEXT("rotation"));

	// relativeTransform → flipX/flipY
	// 矩阵 [[a, b, tx], [c, d, ty]]，用行列式(ad-bc)判断是否有镜像翻转
	// 纯旋转时 det=1，单轴翻转时 det=-1，避免旋转角>90°时cos为负被误判
	const auto* TransformArr = GetArr(NodeObj, TEXT("relativeTransform"));
	if (TransformArr && TransformArr->Num() == 2)
	{
		auto Row0 = (*TransformArr)[0]->AsArray();
		auto Row1 = (*TransformArr)[1]->AsArray();
		if (Row0.Num() == 3 && Row1.Num() == 3)
		{
			float A = Row0[0]->AsNumber();  // cos or -cos(flipped)
			float B = Row0[1]->AsNumber();  // -sin
			float C = Row1[0]->AsNumber();  // sin
			float D = Row1[1]->AsNumber();  // cos or -cos(flipped)
			float Det = A * D - B * C;
			// det < 0 表示有一次镜像翻转
			// 通过分解判断是X翻转还是Y翻转：
			// 去掉旋转分量后，scaleX = sqrt(a²+c²), scaleY = sqrt(b²+d²)
			// 如果 det < 0，翻转轴由 atan2(c,a) vs atan2(-b,d) 的一致性决定
			if (Det < -KINDA_SMALL_NUMBER)
			{
				float ScaleX = FMath::Sqrt(A * A + C * C);
				float ScaleY = FMath::Sqrt(B * B + D * D);
				// Figma 通常只做单轴翻转，用 a 的符号区分：
				// 如果去旋转后 a/scaleX 接近 -1 → X翻转，否则 Y翻转
				float CosFromX = A / FMath::Max(ScaleX, KINDA_SMALL_NUMBER);
				float CosFromY = D / FMath::Max(ScaleY, KINDA_SMALL_NUMBER);
				// 两个cos应该同号（来自同一旋转角），如果异号说明其中一个被翻转
				Node->bFlipX = (CosFromX * CosFromY < 0.0f) ? (CosFromX < 0.0f) : false;
				Node->bFlipY = (CosFromX * CosFromY < 0.0f) ? (CosFromY < 0.0f) : true;
			}
		}
	}

	Node->Type = MapNodeType(FigmaType);
	Node->Metadata.Add(TEXT("sourceNodeType"), FigmaType);

	if (FigmaType == TEXT("COMPONENT"))
		Node->bReusable = true;

	// INSTANCE: 保留组件实例追踪信息（不走 Ref 展开，Figma API 已展开 children）
	if (FigmaType == TEXT("INSTANCE"))
	{
		const FString ComponentId = GetStr(NodeObj, TEXT("componentId"));
		if (!ComponentId.IsEmpty())
		{
			Node->Metadata.Add(TEXT("instanceComponentId"), ComponentId);
		}
	}

	bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;

	// Position
	ParsePosition(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Size
	ParseSize(NodeObj, *Node, bParentAutoLayout);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);

	// Fills
	const auto* FillsArr = GetArr(NodeObj, TEXT("fills"));
	if (FillsArr)
	{
		Node->Fills = ParseFills(*FillsArr);
		Node->Fill = ParseFill(*FillsArr);
	}

	// Stroke
	Node->Stroke = ParseStroke(NodeObj);

	// Vector geometry (for VECTOR / BOOLEAN_OPERATION / LINE / etc.)
	if (Node->Type == EDesignNodeType::Path
		|| Node->Type == EDesignNodeType::Line
		|| Node->Type == EDesignNodeType::Polygon
		|| Node->Type == EDesignNodeType::Ellipse)
	{
		ParsePathGeometry(NodeObj, *Node);
	}

	// Corner radius
	Node->CornerRadius = ParseCornerRadius(NodeObj);

	// Effects
	const auto* EffectsArr = GetArr(NodeObj, TEXT("effects"));
	if (EffectsArr)
		Node->Effects = ParseEffects(*EffectsArr);

	// Text
	if (Node->Type == EDesignNodeType::Text)
		ParseTextProperties(NodeObj, *Node);

	// Children — skip for BOOLEAN_OPERATION/VECTOR (operands, not visual children)
	if (FigmaType != TEXT("BOOLEAN_OPERATION") && FigmaType != TEXT("VECTOR"))
	{
		const auto* ChildrenArr = GetArr(NodeObj, TEXT("children"));
		if (ChildrenArr)
		{
			for (const auto& ChildVal : *ChildrenArr)
			{
				auto ChildObj = ChildVal->AsObject();
				if (auto ChildNode = ParseNode(ChildObj, NodeObj))
					Node->Children.Add(ChildNode);
			}
		}
	}

	ApplyFigmaSemanticHints(NodeObj, *Node);

	return Node;
}

// ---------------------------------------------------------------------------
// ParseDocument
// ---------------------------------------------------------------------------

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	auto Root = ParseJsonString(FigmaJson);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: Failed to parse JSON"));
		return false;
	}

	auto DocObj = GetObj(Root, TEXT("document"));
	if (!DocObj)
	{
		// Fallback: Figma nodes API payload (e.g. /v1/files/{key}/nodes?...),
		// structure is usually Root.nodes["node-id"].document.
		const TSharedPtr<FJsonObject> NodesObj = GetObj(Root, TEXT("nodes"));
		if (NodesObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : NodesObj->Values)
			{
				const TSharedPtr<FJsonObject> NodeEntry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
				if (!NodeEntry.IsValid())
					continue;

				DocObj = GetObj(NodeEntry, TEXT("document"));
				if (DocObj.IsValid())
					break;
			}
		}
	}

	if (!DocObj)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: Missing 'document' field"));
		return false;
	}

	const FString DocType = GetStr(DocObj, TEXT("type"));

	auto CaptureDocumentMetadata = [&OutDocument](const TSharedPtr<FJsonObject>& SourceObj)
	{
		CaptureSharedMetadataExtensions(SourceObj, OutDocument.Metadata);
	};
	CaptureDocumentMetadata(DocObj);
	CaptureDocumentMetadata(Root);

	// Standard file payload: DOCUMENT -> pages -> frames.
	if (DocType == TEXT("DOCUMENT"))
	{
		const auto* Pages = GetArr(DocObj, TEXT("children"));
		if (!Pages)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: No pages found"));
			return true;
		}

		for (const auto& PageVal : *Pages)
		{
			auto PageObj = PageVal->AsObject();
			if (!PageObj) continue;

			const auto* Frames = GetArr(PageObj, TEXT("children"));
			if (!Frames) continue;

			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr))
					OutDocument.Children.Add(Node);
			}
		}
	}
	// Nodes API may directly return FRAME / CANVAS / COMPONENT roots.
	else if (DocType == TEXT("CANVAS"))
	{
		const auto* Frames = GetArr(DocObj, TEXT("children"));
		if (Frames)
		{
			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr))
					OutDocument.Children.Add(Node);
			}
		}
	}
	else
	{
		if (auto Node = ParseNode(DocObj, nullptr))
			OutDocument.Children.Add(Node);
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaParser: Parsed %d top-level frames"), OutDocument.Children.Num());
	return true;
}

// ---------------------------------------------------------------------------
// ExtractFileKey
// ---------------------------------------------------------------------------

bool FFigmaParser::ExtractFileKey(const FString& UrlOrKey, FString& OutKey)
{
	// Match: https://www.figma.com/design/<key>/... or /file/<key>/...
	FRegexPattern Pattern(TEXT("figma\\.com/(?:design|file)/([a-zA-Z0-9]+)"));
	FRegexMatcher Matcher(Pattern, UrlOrKey);

	if (Matcher.FindNext())
	{
		OutKey = Matcher.GetCaptureGroup(1);
		return true;
	}

	// If no URL pattern matched, treat the whole string as a raw key
	if (!UrlOrKey.Contains(TEXT("/")) && !UrlOrKey.Contains(TEXT(".")))
	{
		OutKey = UrlOrKey;
		return true;
	}

	UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Could not extract file key from: %s"), *UrlOrKey);
	return false;
}

// ---------------------------------------------------------------------------
// CollectImageRefs / ApplyImageUrls
// ---------------------------------------------------------------------------

void FFigmaParser::CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs)
{
	CollectImageRefsRecursive(Document.Children, OutImageRefs);
}

void FFigmaParser::CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		// 主填充
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
			OutRefs.Add(Node->Fill.Image.Url);
		// 多层填充
		for (const FDesignFill& F : Node->Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
				OutRefs.Add(F.Image.Url);
		}
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void FFigmaParser::ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
}

void FFigmaParser::ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		// 主填充
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Fill.Image.Url))
				Node->Fill.Image.Url = *Url;
		}
		// 多层填充
		for (FDesignFill& F : Node->Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
			{
				if (const FString* Url = Map.Find(F.Image.Url))
					F.Image.Url = *Url;
			}
		}
		ApplyImageUrlsRecursive(Node->Children, Map);
	}
}


