// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParser.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Internationalization/Regex.h"

static void AddExtensionIfNonEmpty(TMap<FString, FString>& Extensions, const FString& Key, const FString& Value)
{
	if (!Value.IsEmpty())
		Extensions.Add(Key, Value);
}

static FString GetStr(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Default = FString())
{
	return DesignJsonUtils::GetString(Obj, Key, Default);
}

static double GetNum(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default = 0.0)
{
	return DesignJsonUtils::GetNumber(Obj, Key, Default);
}

static bool TryGetNum(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double& OutVal)
{
	return DesignJsonUtils::TryGetNumber(Obj, Key, OutVal);
}

static bool GetBool(const TSharedPtr<FJsonObject>& Obj, const FString& Key, bool Default = true)
{
	return DesignJsonUtils::GetBool(Obj, Key, Default);
}

static TSharedPtr<FJsonObject> GetObj(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	return DesignJsonUtils::GetObject(Obj, Key);
}

static const TArray<TSharedPtr<FJsonValue>>* GetArr(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	return DesignJsonUtils::GetArray(Obj, Key);
}

static FString JsonValueToCompactString(const TSharedPtr<FJsonValue>& Value)
{
	return DesignJsonUtils::ToCompactString(Value);
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
	Extensions.Add(ExtKey, DesignJsonUtils::ToCompactString(FieldValue));
}

static void SetStringFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& OutValue)
{
	if (!Obj.IsValid())
		return;

	const FString Value = GetStr(Obj, Key);
	if (!Value.IsEmpty())
	{
		OutValue = Value;
	}
}

static void SetFloatFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& OutValue, bool& bOutHasValue)
{
	double Value = 0.0;
	if (TryGetNum(Obj, Key, Value))
	{
		OutValue = static_cast<float>(Value);
		bOutHasValue = true;
	}
}

static void SetIntFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& OutValue, bool& bOutHasValue)
{
	double Value = 0.0;
	if (TryGetNum(Obj, Key, Value))
	{
		OutValue = FMath::RoundToInt(Value);
		bOutHasValue = true;
	}
}

static void SetBoolFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool& OutValue, bool& bOutHasValue)
{
	if (!Obj.IsValid())
		return;

	bool Value = false;
	if (Obj->TryGetBoolField(Key, Value))
	{
		OutValue = Value;
		bOutHasValue = true;
	}
}

static void SetJsonFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& OutValue)
{
	if (!Obj.IsValid())
		return;

	const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(Key);
	if (FieldValue.IsValid())
	{
		OutValue = DesignJsonUtils::ToCompactString(FieldValue);
	}
}

static void CaptureRenderFallbackMetadata(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	if (!Obj.IsValid())
		return;

	const struct
	{
		const TCHAR* SourceKey;
		const TCHAR* TargetKey;
	}
	RenderFallbackKeys[] = {
		{ TEXT("design2umgRenderFallbackImageRef"), TEXT("figma.renderFallbackImageRef") },
		{ TEXT("design2umgRenderFallbackScope"), TEXT("figma.renderFallbackScope") },
		{ TEXT("design2umgRenderFallbackSource"), TEXT("figma.renderFallbackSource") },
		{ TEXT("design2umgRenderFallbackReason"), TEXT("figma.renderFallbackReason") },
	};

	for (const auto& KeyPair : RenderFallbackKeys)
	{
		const FString Value = GetStr(Obj, KeyPair.SourceKey);
		if (!Value.IsEmpty())
		{
			Extensions.Add(KeyPair.TargetKey, Value);
		}
	}
}

static void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	if (!Obj.IsValid())
		return;

	for (const TCHAR* Key : { TEXT("overflowDirection"), TEXT("prototypeStartNodeID"), TEXT("transitionNodeID") })
		AddExtensionIfNonEmpty(Extensions, Key, GetStr(Obj, Key));

	for (const TCHAR* Key : { TEXT("layoutGrids"), TEXT("interactions"), TEXT("flowStartingPoints"), TEXT("prototypeDevice"),
		TEXT("componentPropertyReferences"), TEXT("explicitVariableModes"), TEXT("resolvedVariableModes"),
		TEXT("componentPropertyDefinitions"), TEXT("variantGroupProperties"), TEXT("annotations"), TEXT("measurements"),
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
		TEXT("gridChildHorizontalAlign"), TEXT("gridChildVerticalAlign"), TEXT("mainComponentKey"), TEXT("componentSetId") })
	{
		AddExtensionIfNonEmpty(Extensions, Key, GetStr(Obj, Key));
	}

	for (const TCHAR* Key : { TEXT("counterAxisSpacing"), TEXT("minWidth"), TEXT("maxWidth"), TEXT("minHeight"),
		TEXT("maxHeight"), TEXT("gridRowCount"), TEXT("gridColumnCount"), TEXT("gridRowGap"), TEXT("gridColumnGap"),
		TEXT("gridRowSpan"), TEXT("gridColumnSpan"), TEXT("gridColumnAnchorIndex"), TEXT("gridRowAnchorIndex") })
	{
		AddNumericExtensionIfPresent(Obj, Extensions, Key, Key);
	}

	for (const TCHAR* Key : { TEXT("componentProperties"), TEXT("variantProperties"), TEXT("overrides"), TEXT("exposedInstances") })
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

static bool HasNearlyIdenticalBoundingBox(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
{
	const TSharedPtr<FJsonObject> BoundsA = GetObj(A, TEXT("absoluteBoundingBox"));
	const TSharedPtr<FJsonObject> BoundsB = GetObj(B, TEXT("absoluteBoundingBox"));
	if (!BoundsA.IsValid() || !BoundsB.IsValid())
	{
		return false;
	}

	return FMath::IsNearlyEqual(GetNum(BoundsA, TEXT("x")), GetNum(BoundsB, TEXT("x")), 0.5)
		&& FMath::IsNearlyEqual(GetNum(BoundsA, TEXT("y")), GetNum(BoundsB, TEXT("y")), 0.5)
		&& FMath::IsNearlyEqual(GetNum(BoundsA, TEXT("width")), GetNum(BoundsB, TEXT("width")), 0.5)
		&& FMath::IsNearlyEqual(GetNum(BoundsA, TEXT("height")), GetNum(BoundsB, TEXT("height")), 0.5);
}

static bool ShouldSkipAutoLayoutGhostChild(
	const TSharedPtr<FJsonObject>& ChildObj,
	const TArray<TSharedPtr<FJsonValue>>* Siblings)
{
	if (!ChildObj.IsValid() || !Siblings)
	{
		return false;
	}

	if (GetNum(ChildObj, TEXT("opacity"), 1.0) > KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FString ChildName = GetStr(ChildObj, TEXT("name"));
	const FString ChildType = GetStr(ChildObj, TEXT("type"));
	for (const TSharedPtr<FJsonValue>& SiblingValue : *Siblings)
	{
		const TSharedPtr<FJsonObject> SiblingObj = SiblingValue->AsObject();
		if (!SiblingObj.IsValid() || SiblingObj == ChildObj)
		{
			continue;
		}

		if (!GetBool(SiblingObj, TEXT("visible"), true) || GetNum(SiblingObj, TEXT("opacity"), 1.0) <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (GetStr(SiblingObj, TEXT("type")) != ChildType)
		{
			continue;
		}

		if (!ChildName.IsEmpty() && GetStr(SiblingObj, TEXT("name")) != ChildName)
		{
			continue;
		}

		if (HasNearlyIdenticalBoundingBox(ChildObj, SiblingObj))
		{
			return true;
		}
	}

	return false;
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

EDesignNodeType MapNodeType(const FString& FigmaType);
bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj);
FString ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity = 1.0f);
void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode);
void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout);
FDesignFill ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj);
TArray<FDesignFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills);
FDesignFill ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills);
FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& NodeObj);
FDesignCornerRadius ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj);
TArray<FDesignEffect> ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects);
void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode);
void ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout);
TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj);
void CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs);
void ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map);

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

static bool HasImageCropFill(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Fills = GetArr(NodeObj, TEXT("fills"));
	if (!Fills)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillValue.IsValid() ? FillValue->AsObject() : nullptr;
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		{
			continue;
		}

		if (GetStr(FillObj, TEXT("type")) != TEXT("IMAGE") || GetStr(FillObj, TEXT("scaleMode")) != TEXT("CROP"))
		{
			continue;
		}

		return true;
	}

	return false;
}

static bool IsIdentityImageTransformMatrix(const TArray<TSharedPtr<FJsonValue>>* Transform)
{
	if (!Transform || Transform->Num() != 2)
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>> Row0 = (*Transform)[0]->AsArray();
	const TArray<TSharedPtr<FJsonValue>> Row1 = (*Transform)[1]->AsArray();
	if (Row0.Num() != 3 || Row1.Num() != 3)
	{
		return false;
	}

	return FMath::IsNearlyEqual(static_cast<float>(Row0[0]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row0[1]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row0[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[0]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[1]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(Row1[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER);
}

static bool HasFillImageUsingShrunkenRenderBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const FNodeBounds& RenderBounds,
	const FNodeBounds& LayoutBounds)
{
	if (!NodeObj.IsValid() || GetStr(NodeObj, TEXT("type")) != TEXT("RECTANGLE"))
	{
		return false;
	}

	const bool bWidthShrunk = RenderBounds.W + 0.5f < LayoutBounds.W * 0.85f;
	const bool bHeightShrunk = RenderBounds.H + 0.5f < LayoutBounds.H * 0.85f;
	if (!bWidthShrunk && !bHeightShrunk)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Fills = GetArr(NodeObj, TEXT("fills"));
	if (!Fills)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillValue.IsValid() ? FillValue->AsObject() : nullptr;
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		{
			continue;
		}

		if (GetStr(FillObj, TEXT("type")) != TEXT("IMAGE") || GetStr(FillObj, TEXT("scaleMode")) != TEXT("FILL"))
		{
			continue;
		}

		if (IsIdentityImageTransformMatrix(GetArr(FillObj, TEXT("imageTransform"))))
		{
			return true;
		}
	}

	return false;
}

static bool ShouldPreferLayoutBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds& RenderBounds,
	const FNodeBounds& LayoutBounds)
{
	if (HasImageCropFill(NodeObj))
	{
		return true;
	}

	if (HasFillImageUsingShrunkenRenderBounds(NodeObj, RenderBounds, LayoutBounds))
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArr(NodeObj, TEXT("children"));
	const bool bHasChildren = Children && Children->Num() > 0;
	if (bHasChildren)
	{
		const bool bRenderBoundsExpandLayoutBounds =
			RenderBounds.X + 0.5f < LayoutBounds.X
			|| RenderBounds.Y + 0.5f < LayoutBounds.Y
			|| RenderBounds.X + RenderBounds.W > LayoutBounds.X + LayoutBounds.W + 0.5f
			|| RenderBounds.Y + RenderBounds.H > LayoutBounds.Y + LayoutBounds.H + 0.5f;
		if (bRenderBoundsExpandLayoutBounds)
		{
			// Child coordinates are defined in layout space; effect/stroke expansion must not shift the container origin.
			return true;
		}
	}

	const bool bHasLayoutContainer = HasAutoLayout(NodeObj);
	if (bHasLayoutContainer)
	{
		const bool bHasVisibleFill = [&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Fills = GetArr(NodeObj, TEXT("fills"));
			if (!Fills)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& FillVal : *Fills)
			{
				const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
				if (FillObj.IsValid() && GetBool(FillObj, TEXT("visible"), true))
				{
					return true;
				}
			}
			return false;
		}();

		const bool bHasVisibleStroke = [&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArr(NodeObj, TEXT("strokes"));
			if (!Strokes)
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
		}();

		const bool bHasVisibleEffect = [&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Effects = GetArr(NodeObj, TEXT("effects"));
			if (!Effects)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& EffectVal : *Effects)
			{
				const TSharedPtr<FJsonObject> EffectObj = EffectVal->AsObject();
				if (EffectObj.IsValid() && GetBool(EffectObj, TEXT("visible"), true))
				{
					return true;
				}
			}
			return false;
		}();

		const bool bIsLayoutOnlyContainer = !bHasVisibleFill && !bHasVisibleStroke && !bHasVisibleEffect;
		const bool bRenderBoundsShrinkLayoutBounds =
			RenderBounds.W + 0.5f < LayoutBounds.W || RenderBounds.H + 0.5f < LayoutBounds.H;
		if (bIsLayoutOnlyContainer && bRenderBoundsShrinkLayoutBounds)
		{
			if (ParentObj && !HasAutoLayout(ParentObj))
			{
				FNodeBounds ParentLayoutBounds;
				if (TryReadBounds(GetObj(ParentObj, TEXT("absoluteBoundingBox")), ParentLayoutBounds))
				{
					const auto FitsWithin = [](const FNodeBounds& Inner, const FNodeBounds& Outer)
					{
						return Inner.X >= Outer.X - 0.5f
							&& Inner.Y >= Outer.Y - 0.5f
							&& Inner.X + Inner.W <= Outer.X + Outer.W + 0.5f
							&& Inner.Y + Inner.H <= Outer.Y + Outer.H + 0.5f;
					};

					const bool bLayoutEscapesParent = !FitsWithin(LayoutBounds, ParentLayoutBounds);
					const bool bRenderFitsParent = FitsWithin(RenderBounds, ParentLayoutBounds);
					if (bLayoutEscapesParent && bRenderFitsParent)
					{
						return false;
					}
				}
			}

			return true;
		}
	}

	return false;
}

static bool TryGetVisualBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	FNodeBounds& OutBounds)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	FNodeBounds RenderBounds;
	const bool bHasRenderBounds = TryReadBounds(GetObj(NodeObj, TEXT("absoluteRenderBounds")), RenderBounds);
	FNodeBounds LayoutBounds;
	const bool bHasLayoutBounds = TryReadBounds(GetObj(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

	if (bHasRenderBounds)
	{
		if (!bHasLayoutBounds || !ShouldPreferLayoutBounds(NodeObj, ParentObj, RenderBounds, LayoutBounds))
		{
			OutBounds = RenderBounds;
			return true;
		}
	}

	if (!bHasLayoutBounds)
	{
		return false;
	}

	OutBounds = LayoutBounds;

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

EDesignNodeType MapNodeType(const FString& FigmaType)
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

bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (IsWrapLayout(NodeObj) || IsLikelyWrappedHorizontalLayout(NodeObj))
		return true;

	FString Mode = GetStr(NodeObj, TEXT("layoutMode"));
	return Mode == TEXT("HORIZONTAL") || Mode == TEXT("VERTICAL") || Mode == TEXT("GRID");
}

// ---------------------------------------------------------------------------
// ColorToHex
// ---------------------------------------------------------------------------

FString ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity)
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

void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
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

	SetStringFieldIfPresent(NodeObj, TEXT("layoutWrap"), OutNode.FigmaLayout.LayoutWrap);
	SetStringFieldIfPresent(NodeObj, TEXT("counterAxisAlignContent"), OutNode.FigmaLayout.CounterAxisAlignContent);
	SetStringFieldIfPresent(NodeObj, TEXT("layoutPositioning"), OutNode.FigmaLayout.LayoutPositioning);
	SetStringFieldIfPresent(NodeObj, TEXT("layoutAlign"), OutNode.FigmaLayout.LayoutAlign);
	SetStringFieldIfPresent(NodeObj, TEXT("primaryAxisSizingMode"), OutNode.FigmaLayout.PrimaryAxisSizingMode);
	SetStringFieldIfPresent(NodeObj, TEXT("counterAxisSizingMode"), OutNode.FigmaLayout.CounterAxisSizingMode);
	SetStringFieldIfPresent(NodeObj, TEXT("gridRowsSizing"), OutNode.FigmaLayout.GridRowsSizing);
	SetStringFieldIfPresent(NodeObj, TEXT("gridColumnsSizing"), OutNode.FigmaLayout.GridColumnsSizing);
	SetStringFieldIfPresent(NodeObj, TEXT("gridChildHorizontalAlign"), OutNode.FigmaLayout.GridChildHorizontalAlign);
	SetStringFieldIfPresent(NodeObj, TEXT("gridChildVerticalAlign"), OutNode.FigmaLayout.GridChildVerticalAlign);
	SetFloatFieldIfPresent(NodeObj, TEXT("counterAxisSpacing"), OutNode.FigmaLayout.CounterAxisSpacing, OutNode.FigmaLayout.bHasCounterAxisSpacing);
	SetFloatFieldIfPresent(NodeObj, TEXT("minWidth"), OutNode.FigmaLayout.MinWidth, OutNode.FigmaLayout.bHasMinWidth);
	SetFloatFieldIfPresent(NodeObj, TEXT("maxWidth"), OutNode.FigmaLayout.MaxWidth, OutNode.FigmaLayout.bHasMaxWidth);
	SetFloatFieldIfPresent(NodeObj, TEXT("minHeight"), OutNode.FigmaLayout.MinHeight, OutNode.FigmaLayout.bHasMinHeight);
	SetFloatFieldIfPresent(NodeObj, TEXT("maxHeight"), OutNode.FigmaLayout.MaxHeight, OutNode.FigmaLayout.bHasMaxHeight);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowCount"), OutNode.FigmaLayout.GridRowCount, OutNode.FigmaLayout.bHasGridRowCount);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnCount"), OutNode.FigmaLayout.GridColumnCount, OutNode.FigmaLayout.bHasGridColumnCount);
	SetFloatFieldIfPresent(NodeObj, TEXT("gridRowGap"), OutNode.FigmaLayout.GridRowGap, OutNode.FigmaLayout.bHasGridRowGap);
	SetFloatFieldIfPresent(NodeObj, TEXT("gridColumnGap"), OutNode.FigmaLayout.GridColumnGap, OutNode.FigmaLayout.bHasGridColumnGap);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowSpan"), OutNode.FigmaLayout.GridRowSpan, OutNode.FigmaLayout.bHasGridRowSpan);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnSpan"), OutNode.FigmaLayout.GridColumnSpan, OutNode.FigmaLayout.bHasGridColumnSpan);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnAnchorIndex"), OutNode.FigmaLayout.GridColumnAnchorIndex, OutNode.FigmaLayout.bHasGridColumnAnchorIndex);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowAnchorIndex"), OutNode.FigmaLayout.GridRowAnchorIndex, OutNode.FigmaLayout.bHasGridRowAnchorIndex);
	SetBoolFieldIfPresent(NodeObj, TEXT("itemReverseZIndex"), OutNode.FigmaLayout.bItemReverseZIndex, OutNode.FigmaLayout.bHasItemReverseZIndex);
	SetBoolFieldIfPresent(NodeObj, TEXT("strokesIncludedInLayout"), OutNode.FigmaLayout.bStrokesIncludedInLayout, OutNode.FigmaLayout.bHasStrokesIncludedInLayout);
	SetJsonFieldIfPresent(NodeObj, TEXT("boundVariables"), OutNode.FigmaLayout.BoundVariablesJson);
}

// ---------------------------------------------------------------------------
// ParseSize
// ---------------------------------------------------------------------------

void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	FNodeBounds VisualBounds;
	FNodeBounds LayoutBounds;
	const bool bHasVisualBounds = TryGetVisualBounds(NodeObj, ParentObj, VisualBounds);
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

#include "FigmaParserStyling.h"

// ---------------------------------------------------------------------------
// ParsePosition
// ---------------------------------------------------------------------------

void ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.bHasX = false;
		OutNode.bHasY = false;
		return;
	}

	FNodeBounds ChildBounds;
	const bool bHasChildBounds = TryGetVisualBounds(NodeObj, ParentObj, ChildBounds);

	FNodeBounds ParentBounds;
	const bool bHasParentBounds = ParentObj && TryGetVisualBounds(ParentObj, nullptr, ParentBounds);

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

TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj) return nullptr;

	FString FigmaType = GetStr(NodeObj, TEXT("type"));
	if (FigmaType.IsEmpty()) return nullptr;

	// Figma mask 节点是裁剪几何，不应直接生成可视控件。
	const bool bIsMaskNode = GetBool(NodeObj, TEXT("isMask"), false);
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
		Node->FigmaInstance.bIsInstance = true;
		const FString ComponentId = GetStr(NodeObj, TEXT("componentId"));
		if (!ComponentId.IsEmpty())
		{
			Node->FigmaInstance.ComponentId = ComponentId;
			Node->Metadata.Add(TEXT("instanceComponentId"), ComponentId);
		}
		SetStringFieldIfPresent(NodeObj, TEXT("componentSetId"), Node->FigmaInstance.ComponentSetId);
		SetStringFieldIfPresent(NodeObj, TEXT("mainComponentKey"), Node->FigmaInstance.MainComponentKey);
		SetJsonFieldIfPresent(NodeObj, TEXT("componentProperties"), Node->FigmaInstance.ComponentPropertiesJson);
		SetJsonFieldIfPresent(NodeObj, TEXT("variantProperties"), Node->FigmaInstance.VariantPropertiesJson);
		SetJsonFieldIfPresent(NodeObj, TEXT("overrides"), Node->FigmaInstance.OverridesJson);
		SetBoolFieldIfPresent(NodeObj, TEXT("isExposedInstance"), Node->FigmaInstance.bIsExposedInstance, Node->FigmaInstance.bHasIsExposedInstance);
	}

	bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;

	// Position
	ParsePosition(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Size
	ParseSize(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);

	// Fills
	const auto* FillsArr = GetArr(NodeObj, TEXT("fills"));
	if (FillsArr)
	{
		Node->Fills = ParseFills(*FillsArr);
		Node->Fill = ParseFill(*FillsArr);
		const FString VectorRasterFallback = ExtractSyntheticVectorRasterImageRef(*FillsArr);
		if (!VectorRasterFallback.IsEmpty())
		{
			Node->Metadata.Add(TEXT("figma.vectorRasterFallback"), VectorRasterFallback);
		}
	}
	CaptureRenderFallbackMetadata(NodeObj, Node->Metadata);

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
				if (bParentAutoLayout && ShouldSkipAutoLayoutGhostChild(ChildObj, ChildrenArr))
				{
					continue;
				}
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
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("components"), TEXT("components"));
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("componentSets"), TEXT("componentSets"));
	SetJsonFieldIfPresent(Root, TEXT("components"), OutDocument.FigmaDocument.ComponentsJson);
	SetJsonFieldIfPresent(Root, TEXT("componentSets"), OutDocument.FigmaDocument.ComponentSetsJson);

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

void CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
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
		if (const FString* VectorRasterFallback = Node->Metadata.Find(TEXT("figma.vectorRasterFallback")))
		{
			if (!VectorRasterFallback->IsEmpty())
				OutRefs.Add(*VectorRasterFallback);
		}
		if (const FString* RenderFallback = Node->Metadata.Find(TEXT("figma.renderFallbackImageRef")))
		{
			if (!RenderFallback->IsEmpty())
				OutRefs.Add(*RenderFallback);
		}
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void FFigmaParser::ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
}

void ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map)
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
		if (FString* VectorRasterFallback = Node->Metadata.Find(TEXT("figma.vectorRasterFallback")))
		{
			if (const FString* Url = Map.Find(*VectorRasterFallback))
				*VectorRasterFallback = *Url;
		}
		if (FString* RenderFallback = Node->Metadata.Find(TEXT("figma.renderFallbackImageRef")))
		{
			if (const FString* Url = Map.Find(*RenderFallback))
				*RenderFallback = *Url;
		}
		ApplyImageUrlsRecursive(Node->Children, Map);
	}
}




