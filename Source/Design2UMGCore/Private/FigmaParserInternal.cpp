// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserInternal.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserPrototype.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Internationalization/Regex.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetArray;
using DesignJsonUtils::GetBool;
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
using DesignJsonUtils::GetString;
using DesignJsonUtils::ToCompactString;
using DesignJsonUtils::TryGetNumber;

static void AddExtensionIfNonEmpty(TMap<FString, FString>& Extensions, const FString& Key, const FString& Value)
{
	if (!Value.IsEmpty())
		Extensions.Add(Key, Value);
}

static void AddNumericExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& SourceKey, const FString& ExtKey)
{
	double Val = 0.0;
	if (TryGetNumber(Obj, SourceKey, Val))
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

	const FString Value = GetString(Obj, Key);
	if (!Value.IsEmpty())
	{
		OutValue = Value;
	}
}

static void SetFloatFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& OutValue, bool& bOutHasValue)
{
	double Value = 0.0;
	if (TryGetNumber(Obj, Key, Value))
	{
		OutValue = static_cast<float>(Value);
		bOutHasValue = true;
	}
}

static void SetIntFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& OutValue, bool& bOutHasValue)
{
	double Value = 0.0;
	if (TryGetNumber(Obj, Key, Value))
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

static void PromoteFigmaGridLayout(FDesignNode& OutNode)
{
	const FDesignSourceNodeData& SourceData = OutNode.Source();
	if (SourceData.LayoutExtensions.bHasGridRowCount)
	{
		OutNode.LayoutData.SetLayoutRowCountValue(FMath::Max(0, SourceData.LayoutExtensions.GridRowCount));
	}
	if (SourceData.LayoutExtensions.bHasGridColumnCount)
	{
		OutNode.LayoutData.SetLayoutColumnCountValue(FMath::Max(0, SourceData.LayoutExtensions.GridColumnCount));
	}
	if (SourceData.LayoutExtensions.bHasGridRowGap)
	{
		OutNode.LayoutData.SetLayoutRowGapValue(FMath::Max(0.0f, SourceData.LayoutExtensions.GridRowGap));
	}
	else if (SourceData.LayoutExtensions.bHasCounterAxisSpacing)
	{
		OutNode.LayoutData.SetLayoutRowGapValue(FMath::Max(0.0f, SourceData.LayoutExtensions.CounterAxisSpacing));
	}
	if (SourceData.LayoutExtensions.bHasGridColumnGap)
	{
		OutNode.LayoutData.SetLayoutColumnGapValue(FMath::Max(0.0f, SourceData.LayoutExtensions.GridColumnGap));
	}
	if (!SourceData.LayoutExtensions.GridChildHorizontalAlign.IsEmpty())
	{
		OutNode.LayoutData.SetLayoutChildHorizontalAlignValue(SourceData.LayoutExtensions.GridChildHorizontalAlign);
	}
	if (!SourceData.LayoutExtensions.GridChildVerticalAlign.IsEmpty())
	{
		OutNode.LayoutData.SetLayoutChildVerticalAlignValue(SourceData.LayoutExtensions.GridChildVerticalAlign);
	}
	if (SourceData.LayoutExtensions.bHasGridRowSpan)
	{
		OutNode.LayoutData.SetLayoutRowSpanValue(FMath::Max(1, SourceData.LayoutExtensions.GridRowSpan));
	}
	if (SourceData.LayoutExtensions.bHasGridColumnSpan)
	{
		OutNode.LayoutData.SetLayoutColumnSpanValue(FMath::Max(1, SourceData.LayoutExtensions.GridColumnSpan));
	}
	if (SourceData.LayoutExtensions.bHasGridRowAnchorIndex)
	{
		OutNode.LayoutData.SetLayoutRowValue(FMath::Max(0, SourceData.LayoutExtensions.GridRowAnchorIndex));
	}
	if (SourceData.LayoutExtensions.bHasGridColumnAnchorIndex)
	{
		OutNode.LayoutData.SetLayoutColumnValue(FMath::Max(0, SourceData.LayoutExtensions.GridColumnAnchorIndex));
	}
}

static bool IsStretchAutoLayoutChild(const FDesignNode& ParentNode, const TSharedPtr<FDesignNode>& Child)
{
	if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
	{
		return false;
	}

	if (Child->Source().LayoutExtensions.LayoutAlign.Equals(TEXT("STRETCH"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (ParentNode.LayoutData.Layout == EDesignLayout::Vertical)
	{
		return Child->Transform.Width.IsFill();
	}

	if (ParentNode.LayoutData.Layout == EDesignLayout::Horizontal)
	{
		return Child->Transform.Height.IsFill();
	}

	return false;
}

static bool ShouldTopLevelFigmaInstancePaddingWrapAtRuntime(const FDesignNode& Node)
{
	bool bHasStretchAutoChild = false;
	bool bHasNonStretchAutoChild = false;
	for (const TSharedPtr<FDesignNode>& Child : Node.Children)
	{
		if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
		{
			continue;
		}

		if (IsStretchAutoLayoutChild(Node, Child))
		{
			bHasStretchAutoChild = true;
		}
		else
		{
			bHasNonStretchAutoChild = true;
		}

		if (bHasStretchAutoChild && bHasNonStretchAutoChild)
		{
			return true;
		}
	}

	return false;
}

static bool ShouldFigmaAutoLayoutPaddingWrapAtRuntime(const FDesignNode& Node, const bool bHasInstanceAncestor)
{
	if (Node.LayoutData.Layout != EDesignLayout::Horizontal && Node.LayoutData.Layout != EDesignLayout::Vertical)
	{
		return false;
	}

	if (bHasInstanceAncestor)
	{
		return false;
	}

	if (Node.Source().ComponentInstance.bIsInstance)
	{
		return ShouldTopLevelFigmaInstancePaddingWrapAtRuntime(Node);
	}

	for (const TSharedPtr<FDesignNode>& Child : Node.Children)
	{
		if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
		{
			continue;
		}

		return true;
	}

	return false;
}

static void CaptureRenderFallbackMetadata(const TSharedPtr<FJsonObject>& Obj, FDesignNode& OutNode)
{
	if (!Obj.IsValid())
		return;

	const struct
	{
		const TCHAR* SourceKey;
		FString FDesignRenderFallbackData::* TargetField;
	}
	RenderFallbackKeys[] = {
		{ TEXT("design2umgRenderFallbackImageRef"), &FDesignRenderFallbackData::ImageRef },
		{ TEXT("design2umgRenderFallbackScope"), &FDesignRenderFallbackData::Scope },
		{ TEXT("design2umgRenderFallbackSource"), &FDesignRenderFallbackData::Source },
		{ TEXT("design2umgRenderFallbackReason"), &FDesignRenderFallbackData::Reason },
		{ TEXT("design2umgRenderFallbackOffsetX"), &FDesignRenderFallbackData::OffsetX },
		{ TEXT("design2umgRenderFallbackOffsetY"), &FDesignRenderFallbackData::OffsetY },
		{ TEXT("design2umgRenderFallbackWidth"), &FDesignRenderFallbackData::Width },
		{ TEXT("design2umgRenderFallbackHeight"), &FDesignRenderFallbackData::Height },
	};

	for (const auto& KeyPair : RenderFallbackKeys)
	{
		const FString Value = GetString(Obj, KeyPair.SourceKey);
		if (!Value.IsEmpty())
		{
			OutNode.MutableSource().RenderFallback.*(KeyPair.TargetField) = Value;
		}
	}
}

static void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	if (!Obj.IsValid())
		return;

	for (const TCHAR* Key : { TEXT("overflowDirection"), TEXT("prototypeStartNodeID"), TEXT("transitionNodeID") })
		AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));

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
		AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));
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

static void AppendUniqueSkippedDesignerNode(
	TArray<FDesignSkippedNodeReport>& Reports,
	const FString& NodeId,
	const FString& NodeName,
	const FString& NodeType,
	const FString& Reason)
{
	for (const FDesignSkippedNodeReport& Existing : Reports)
	{
		if (Existing.NodeId == NodeId
			&& Existing.NodeType == NodeType
			&& Existing.Reason == Reason)
		{
			return;
		}
	}

	FDesignSkippedNodeReport Report;
	Report.NodeId = NodeId;
	Report.NodeName = NodeName;
	Report.NodeType = NodeType;
	Report.Reason = Reason;
	Reports.Add(MoveTemp(Report));
}

static TSharedPtr<FJsonObject> ParseJsonString(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}

static bool HasOpaqueVisibleSolidFill(const TSharedPtr<FJsonObject>& NodeObj)
{
	const auto* Fills = GetArray(NodeObj, TEXT("fills"));
	if (!Fills || Fills->Num() == 0)
		return false;

	int32 VisibleFillCount = 0;
	TSharedPtr<FJsonObject> VisibleSolidFill;
	for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
	{
		const TSharedPtr<FJsonObject> FillObj = FillValue->AsObject();
		if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		{
			continue;
		}

		++VisibleFillCount;
		if (VisibleFillCount > 1)
		{
			return false;
		}

		VisibleSolidFill = FillObj;
	}

	if (!VisibleSolidFill.IsValid() || GetString(VisibleSolidFill, TEXT("type")) != TEXT("SOLID"))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> ColorObj = GetObject(VisibleSolidFill, TEXT("color"));
	if (!ColorObj.IsValid())
		return false;

	const double A = GetNumber(ColorObj, TEXT("a"), 1.0);
	return A >= 0.99;
}

static bool IsWrapLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	const FString Wrap = GetString(NodeObj, TEXT("layoutWrap"));
	return Wrap.Equals(TEXT("WRAP"), ESearchCase::IgnoreCase);
}

static bool IsLikelyWrappedHorizontalLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid() || GetString(NodeObj, TEXT("layoutMode")) != TEXT("HORIZONTAL"))
		return false;

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
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

		if (GetString(ChildObj, TEXT("layoutSizingHorizontal")) == TEXT("FILL"))
			++FillChildren;

		if (const TSharedPtr<FJsonObject> BoundsObj = GetObject(ChildObj, TEXT("absoluteBoundingBox")))
			ChildRowsY.Add(GetNumber(BoundsObj, TEXT("y")));
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
	const TSharedPtr<FJsonObject> ChildBBox = GetObject(NodeObj, TEXT("absoluteBoundingBox"));
	const TSharedPtr<FJsonObject> ParentBBox = GetObject(ParentObj, TEXT("absoluteBoundingBox"));
	if (!ChildBBox.IsValid() || !ParentBBox.IsValid())
		return false;

	const double CX = GetNumber(ChildBBox, TEXT("x"));
	const double CY = GetNumber(ChildBBox, TEXT("y"));
	const double CW = GetNumber(ChildBBox, TEXT("width"));
	const double CH = GetNumber(ChildBBox, TEXT("height"));
	const double PX = GetNumber(ParentBBox, TEXT("x"));
	const double PY = GetNumber(ParentBBox, TEXT("y"));
	const double PW = GetNumber(ParentBBox, TEXT("width"));
	const double PH = GetNumber(ParentBBox, TEXT("height"));
	return FMath::IsNearlyEqual(CX, PX, 0.5) &&
		FMath::IsNearlyEqual(CY, PY, 0.5) &&
		FMath::IsNearlyEqual(CW, PW, 0.5) &&
		FMath::IsNearlyEqual(CH, PH, 0.5);
}

static bool HasRenderableGeometryWithoutChildren(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	if (Children && Children->Num() > 0)
	{
		return false;
	}

	const FString FigmaType = GetString(NodeObj, TEXT("type"));
	return FigmaType == TEXT("RECTANGLE")
		|| FigmaType == TEXT("ELLIPSE")
		|| FigmaType == TEXT("POLYGON")
		|| FigmaType == TEXT("STAR")
		|| FigmaType == TEXT("VECTOR")
		|| FigmaType == TEXT("BOOLEAN_OPERATION");
}

static bool HasVisibleStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArray(NodeObj, TEXT("strokes"));
	if (!Strokes || Strokes->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& StrokeValue : *Strokes)
	{
		const TSharedPtr<FJsonObject> StrokeObj = StrokeValue->AsObject();
		if (StrokeObj.IsValid() && GetBool(StrokeObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

static bool HasVisibleEffect(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Effects = GetArray(NodeObj, TEXT("effects"));
	if (!Effects || Effects->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
	{
		const TSharedPtr<FJsonObject> EffectObj = EffectValue->AsObject();
		if (EffectObj.IsValid() && GetBool(EffectObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

static bool HasVisibleStrokesOrEffects(const TSharedPtr<FJsonObject>& NodeObj)
{
	return HasVisibleStroke(NodeObj) || HasVisibleEffect(NodeObj);
}

static int32 FindChildIndexById(const TSharedPtr<FJsonObject>& ParentObj, const FString& ChildId)
{
	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(ParentObj, TEXT("children"));
	if (!Children)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> ChildObj = (*Children)[Index]->AsObject();
		if (ChildObj.IsValid() && GetString(ChildObj, TEXT("id")) == ChildId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

static bool IsLikelyImplicitMaskLeaf(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj.IsValid() || !ParentObj.IsValid())
	{
		return false;
	}

	if (GetString(ParentObj, TEXT("type")) != TEXT("GROUP"))
	{
		return false;
	}

	if (!HasRenderableGeometryWithoutChildren(NodeObj)
		|| HasVisibleStrokesOrEffects(NodeObj)
		|| !HasOpaqueVisibleSolidFill(NodeObj)
		|| !HasSameBoundsAsParent(NodeObj, ParentObj))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(ParentObj, TEXT("children"));
	if (!Children || Children->Num() <= 1)
	{
		return false;
	}

	return FindChildIndexById(ParentObj, GetString(NodeObj, TEXT("id"))) == 0;
}

static bool HasNearlyIdenticalBoundingBox(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
{
	const TSharedPtr<FJsonObject> BoundsA = GetObject(A, TEXT("absoluteBoundingBox"));
	const TSharedPtr<FJsonObject> BoundsB = GetObject(B, TEXT("absoluteBoundingBox"));
	if (!BoundsA.IsValid() || !BoundsB.IsValid())
	{
		return false;
	}

	return FMath::IsNearlyEqual(GetNumber(BoundsA, TEXT("x")), GetNumber(BoundsB, TEXT("x")), 0.5)
		&& FMath::IsNearlyEqual(GetNumber(BoundsA, TEXT("y")), GetNumber(BoundsB, TEXT("y")), 0.5)
		&& FMath::IsNearlyEqual(GetNumber(BoundsA, TEXT("width")), GetNumber(BoundsB, TEXT("width")), 0.5)
		&& FMath::IsNearlyEqual(GetNumber(BoundsA, TEXT("height")), GetNumber(BoundsB, TEXT("height")), 0.5);
}

static bool ShouldSkipAutoLayoutGhostChild(
	const TSharedPtr<FJsonObject>& ChildObj,
	const TArray<TSharedPtr<FJsonValue>>* Siblings)
{
	if (!ChildObj.IsValid() || !Siblings)
	{
		return false;
	}

	if (GetNumber(ChildObj, TEXT("opacity"), 1.0) > KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FString ChildName = GetString(ChildObj, TEXT("name"));
	const FString ChildType = GetString(ChildObj, TEXT("type"));
	for (const TSharedPtr<FJsonValue>& SiblingValue : *Siblings)
	{
		const TSharedPtr<FJsonObject> SiblingObj = SiblingValue->AsObject();
		if (!SiblingObj.IsValid() || SiblingObj == ChildObj)
		{
			continue;
		}

		if (!GetBool(SiblingObj, TEXT("visible"), true) || GetNumber(SiblingObj, TEXT("opacity"), 1.0) <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (GetString(SiblingObj, TEXT("type")) != ChildType)
		{
			continue;
		}

		if (!ChildName.IsEmpty() && GetString(SiblingObj, TEXT("name")) != ChildName)
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

		const FString Path = GetString(GeometryObj, TEXT("path"));
		if (Path.IsEmpty())
			continue;

		OutPath = Path;
		OutRule = GetString(GeometryObj, TEXT("windingRule"));
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
TSharedPtr<FDesignNode> ParseNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	bool bInsideInstanceSubtree);
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

		// pluginData is usually stored as a JSON string. Parse it as JSON first.
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
		Node.Metadata.Add(TEXT("source.slot.present"), TEXT("true"));
		Node.Metadata.Add(TEXT("source.slot.count"), FString::FromInt(SlotCount));
		if (SlotCount == 0)
			Node.Metadata.Add(TEXT("source.slot.empty"), TEXT("true"));
	}

	const bool bExplicitPlaceholder = ReadSemanticSignalField(PlaceholderField);
	const bool bExplicitSlot = ReadSemanticSignalField(SlotField);

	const bool bHasPluginSemanticMarker =
		TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("pluginData")))
		|| TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("sharedPluginData")));

	Node.LayoutData.bSemanticPlaceholderSurface =
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

	OutBounds.X = GetNumber(BoundsObj, TEXT("x"));
	OutBounds.Y = GetNumber(BoundsObj, TEXT("y"));
	OutBounds.W = GetNumber(BoundsObj, TEXT("width"));
	OutBounds.H = GetNumber(BoundsObj, TEXT("height"));
	return OutBounds.W > 0.0f && OutBounds.H > 0.0f;
}

static void CaptureBoundsDeltaMetadata(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	FNodeBounds LayoutBounds;
	FNodeBounds RenderBounds;
	if (!TryReadBounds(GetObject(Obj, TEXT("absoluteBoundingBox")), LayoutBounds)
		|| !TryReadBounds(GetObject(Obj, TEXT("absoluteRenderBounds")), RenderBounds))
	{
		return;
	}

	const auto AddDeltaIfNonZero = [&Extensions](const TCHAR* Key, const float Delta)
	{
		if (!FMath::IsNearlyZero(Delta, 0.001f))
		{
			Extensions.Add(Key, FString::SanitizeFloat(Delta));
		}
	};

	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaX"), RenderBounds.X - LayoutBounds.X);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaY"), RenderBounds.Y - LayoutBounds.Y);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaW"), RenderBounds.W - LayoutBounds.W);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaH"), RenderBounds.H - LayoutBounds.H);
}

static bool HasVisibleForegroundBlurEffect(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Effects = GetArray(NodeObj, TEXT("effects"));
	if (!Effects)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
	{
		const TSharedPtr<FJsonObject> EffectObj = EffectValue.IsValid() ? EffectValue->AsObject() : nullptr;
		if (!EffectObj.IsValid() || !GetBool(EffectObj, TEXT("visible"), true))
		{
			continue;
		}

		if (GetString(EffectObj, TEXT("type")) == TEXT("LAYER_BLUR") && GetNumber(EffectObj, TEXT("radius")) > 0.0)
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
	const TSharedPtr<FJsonObject> IndividualWeights = GetObject(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights.IsValid())
	{
		Top = GetNumber(IndividualWeights, TEXT("top"));
		Right = GetNumber(IndividualWeights, TEXT("right"));
		Bottom = GetNumber(IndividualWeights, TEXT("bottom"));
		Left = GetNumber(IndividualWeights, TEXT("left"));
	}
	else
	{
		const float Uniform = GetNumber(NodeObj, TEXT("strokeWeight"));
		Top = Uniform;
		Right = Uniform;
		Bottom = Uniform;
		Left = Uniform;
	}

	const FString Align = GetString(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
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
	const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
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

		if (GetString(FillObj, TEXT("type")) != TEXT("IMAGE") || GetString(FillObj, TEXT("scaleMode")) != TEXT("CROP"))
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
	if (!NodeObj.IsValid() || GetString(NodeObj, TEXT("type")) != TEXT("RECTANGLE"))
	{
		return false;
	}

	const bool bWidthShrunk = RenderBounds.W + 0.5f < LayoutBounds.W * 0.85f;
	const bool bHeightShrunk = RenderBounds.H + 0.5f < LayoutBounds.H * 0.85f;
	if (!bWidthShrunk && !bHeightShrunk)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
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

		if (GetString(FillObj, TEXT("type")) != TEXT("IMAGE") || GetString(FillObj, TEXT("scaleMode")) != TEXT("FILL"))
		{
			continue;
		}

		if (IsIdentityImageTransformMatrix(GetArray(FillObj, TEXT("imageTransform"))))
		{
			return true;
		}
	}

	return false;
}

static bool IsContainerLikeNodeTypeForLayoutBounds(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	const FString NodeType = GetString(NodeObj, TEXT("type"));
	return NodeType == TEXT("FRAME")
		|| NodeType == TEXT("GROUP")
		|| NodeType == TEXT("TRANSFORM_GROUP")
		|| NodeType == TEXT("INSTANCE")
		|| NodeType == TEXT("COMPONENT")
		|| NodeType == TEXT("COMPONENT_SET")
		|| NodeType == TEXT("SECTION");
}

static bool ShouldSkipFigmaNodeType(const FString& FigmaType)
{
	return FigmaType == TEXT("SLICE")
		|| FigmaType == TEXT("TABLE")
		|| FigmaType == TEXT("TABLE_CELL")
		|| FigmaType == TEXT("STICKY")
		|| FigmaType == TEXT("SHAPE_WITH_TEXT")
		|| FigmaType == TEXT("CONNECTOR")
		|| FigmaType == TEXT("WASHI_TAPE");
}

static bool ShouldPreferLayoutBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds& RenderBounds,
	const FNodeBounds& LayoutBounds)
{
	const FString NodeType = GetString(NodeObj, TEXT("type"));
	const bool bRenderBoundsExpandLayoutBounds =
		RenderBounds.X + 0.5f < LayoutBounds.X
		|| RenderBounds.Y + 0.5f < LayoutBounds.Y
		|| RenderBounds.X + RenderBounds.W > LayoutBounds.X + LayoutBounds.W + 0.5f
		|| RenderBounds.Y + RenderBounds.H > LayoutBounds.Y + LayoutBounds.H + 0.5f;
	const bool bRenderBoundsShrinkLayoutBounds =
		RenderBounds.W + 0.5f < LayoutBounds.W || RenderBounds.H + 0.5f < LayoutBounds.H;

	if (HasImageCropFill(NodeObj))
	{
		return true;
	}

	if (HasFillImageUsingShrunkenRenderBounds(NodeObj, RenderBounds, LayoutBounds))
	{
		return true;
	}

	if (bRenderBoundsExpandLayoutBounds && HasVisibleForegroundBlurEffect(NodeObj))
	{
		// Foreground blur is not faithfully reproduced by the shared UMG path yet.
		// Keep geometry in layout space so the ignored blur does not inflate the node.
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	const bool bHasChildren = Children && Children->Num() > 0;
	if (bHasChildren)
	{
		if (bRenderBoundsExpandLayoutBounds)
		{
			// Child coordinates are defined in layout space; effect/stroke expansion must not shift the container origin.
			return true;
		}
	}

	const auto ShouldKeepShrunkenRenderBounds = [&]() -> bool
	{
		if (!ParentObj || HasAutoLayout(ParentObj))
		{
			return false;
		}

		FNodeBounds ParentLayoutBounds;
		if (!TryReadBounds(GetObject(ParentObj, TEXT("absoluteBoundingBox")), ParentLayoutBounds))
		{
			return false;
		}

		const auto FitsWithin = [](const FNodeBounds& Inner, const FNodeBounds& Outer)
		{
			return Inner.X >= Outer.X - 0.5f
				&& Inner.Y >= Outer.Y - 0.5f
				&& Inner.X + Inner.W <= Outer.X + Outer.W + 0.5f
				&& Inner.Y + Inner.H <= Outer.Y + Outer.H + 0.5f;
		};

		const bool bLayoutEscapesParent = !FitsWithin(LayoutBounds, ParentLayoutBounds);
		const bool bRenderFitsParent = FitsWithin(RenderBounds, ParentLayoutBounds);
		return bLayoutEscapesParent && bRenderFitsParent;
	};

	if (IsContainerLikeNodeTypeForLayoutBounds(NodeObj) && bRenderBoundsShrinkLayoutBounds)
	{
		return !ShouldKeepShrunkenRenderBounds();
	}

	const bool bHasLayoutContainer = HasAutoLayout(NodeObj);
	if (bHasLayoutContainer)
	{
		const bool bHasVisibleFill = [&]()
		{
			const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
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

		const bool bHasVisibleStroke = HasVisibleStroke(NodeObj);
		const bool bHasVisibleEffect = HasVisibleEffect(NodeObj);

		const bool bIsLayoutOnlyContainer = !bHasVisibleFill && !bHasVisibleStroke && !bHasVisibleEffect;
		if (bIsLayoutOnlyContainer && bRenderBoundsShrinkLayoutBounds)
		{
			return !ShouldKeepShrunkenRenderBounds();
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
	const bool bHasRenderBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteRenderBounds")), RenderBounds);
	FNodeBounds LayoutBounds;
	const bool bHasLayoutBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

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

	// If render bounds are unavailable, expand layout bounds by stroke alignment.
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
	if (!ParseGeometryArray(GetArray(NodeObj, TEXT("fillGeometry")), GeometryPath, WindingRule))
	{
		ParseGeometryArray(GetArray(NodeObj, TEXT("strokeGeometry")), GeometryPath, WindingRule);
	}

	if (!GeometryPath.IsEmpty())
	{
		OutNode.Shape.Geometry = GeometryPath;
		if (!WindingRule.IsEmpty())
		{
			OutNode.Shape.FillRule = NormalizeWindingRule(WindingRule);
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
		return EDesignNodeType::Frame;  // The Figma API already expands INSTANCE children, so treat it as a Frame.
	if (FigmaType == TEXT("GROUP") || FigmaType == TEXT("TRANSFORM_GROUP"))
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
	if (FigmaType == TEXT("TEXT") || FigmaType == TEXT("TEXT_PATH"))
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

	FString Mode = GetString(NodeObj, TEXT("layoutMode"));
	return Mode == TEXT("HORIZONTAL") || Mode == TEXT("VERTICAL") || Mode == TEXT("GRID");
}

// ---------------------------------------------------------------------------
// ColorToHex
// ---------------------------------------------------------------------------

FString ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity)
{
	if (!ColorObj) return TEXT("#000000");

	uint8 R = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("r")) * 255.0), 0, 255);
	uint8 G = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("g")) * 255.0), 0, 255);
	uint8 B = FMath::Clamp(FMath::RoundToInt(GetNumber(ColorObj, TEXT("b")) * 255.0), 0, 255);
	float A = GetNumber(ColorObj, TEXT("a"), 1.0) * Opacity;
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
	FString Mode = GetString(NodeObj, TEXT("layoutMode"));
	if (bWrapLayout)
	{
		OutNode.LayoutData.Layout = EDesignLayout::Wrap;
	}
	else if (Mode == TEXT("HORIZONTAL"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Horizontal;
	}
	else if (Mode == TEXT("VERTICAL"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Vertical;
	}
	else if (Mode == TEXT("GRID"))
	{
		OutNode.LayoutData.Layout = EDesignLayout::Grid;
	}
	else
	{
		OutNode.LayoutData.Layout = EDesignLayout::None;
	}

	OutNode.LayoutData.Gap = GetNumber(NodeObj, TEXT("itemSpacing"));

	OutNode.LayoutData.Padding.Left = GetNumber(NodeObj, TEXT("paddingLeft"));
	OutNode.LayoutData.Padding.Right = GetNumber(NodeObj, TEXT("paddingRight"));
	OutNode.LayoutData.Padding.Top = GetNumber(NodeObj, TEXT("paddingTop"));
	OutNode.LayoutData.Padding.Bottom = GetNumber(NodeObj, TEXT("paddingBottom"));
	OutNode.LayoutData.bAbsoluteLayoutPositioning = GetString(NodeObj, TEXT("layoutPositioning")).Equals(TEXT("ABSOLUTE"), ESearchCase::IgnoreCase);

	{
		CaptureLayoutMetadataExtensions(NodeObj, OutNode.Metadata);
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("itemReverseZIndex"));
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("isExposedInstance"));
		AddBoolExtensionIfPresent(NodeObj, OutNode.Metadata, TEXT("strokesIncludedInLayout"));

		const TSharedPtr<FJsonValue> BoundVarsField = NodeObj->TryGetField(TEXT("boundVariables"));
		if (BoundVarsField.IsValid() && BoundVarsField->Type == EJson::Object)
		{
			OutNode.Metadata.Add(TEXT("boundVariables"), ToCompactString(BoundVarsField));
			const TSharedPtr<FJsonObject> BoundVarsObj = BoundVarsField->AsObject();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BoundVarsObj->Values)
			{
				OutNode.Metadata.Add(FString::Printf(TEXT("boundVariables.%s"), *Pair.Key), ToCompactString(Pair.Value));
			}
		}
	}

	{
		FString Primary = GetString(NodeObj, TEXT("primaryAxisAlignItems"));
		if (Primary == TEXT("CENTER"))
			OutNode.LayoutData.JustifyContent = EDesignJustifyContent::Center;
		else if (Primary == TEXT("MAX"))
			OutNode.LayoutData.JustifyContent = EDesignJustifyContent::End;
		else if (Primary == TEXT("SPACE_BETWEEN"))
			OutNode.LayoutData.JustifyContent = EDesignJustifyContent::SpaceBetween;
		else
			OutNode.LayoutData.JustifyContent = EDesignJustifyContent::Start;

		FString Counter = GetString(NodeObj, TEXT("counterAxisAlignItems"));
		if (Counter == TEXT("CENTER"))
			OutNode.LayoutData.AlignItems = EDesignAlignItems::Center;
		else if (Counter == TEXT("MAX"))
			OutNode.LayoutData.AlignItems = EDesignAlignItems::End;
		else
			OutNode.LayoutData.AlignItems = EDesignAlignItems::Start;
	}

	OutNode.LayoutData.SetClipValue(GetBool(NodeObj, TEXT("clipsContent"), false));

	FDesignLayoutExtensions& LayoutExtensions = OutNode.MutableSource().LayoutExtensions;
	SetStringFieldIfPresent(NodeObj, TEXT("layoutWrap"), LayoutExtensions.LayoutWrap);
	SetStringFieldIfPresent(NodeObj, TEXT("counterAxisAlignContent"), LayoutExtensions.CounterAxisAlignContent);
	SetStringFieldIfPresent(NodeObj, TEXT("layoutPositioning"), LayoutExtensions.LayoutPositioning);
	SetStringFieldIfPresent(NodeObj, TEXT("layoutAlign"), LayoutExtensions.LayoutAlign);
	SetStringFieldIfPresent(NodeObj, TEXT("primaryAxisSizingMode"), LayoutExtensions.PrimaryAxisSizingMode);
	SetStringFieldIfPresent(NodeObj, TEXT("counterAxisSizingMode"), LayoutExtensions.CounterAxisSizingMode);
	SetStringFieldIfPresent(NodeObj, TEXT("gridRowsSizing"), LayoutExtensions.GridRowsSizing);
	SetStringFieldIfPresent(NodeObj, TEXT("gridColumnsSizing"), LayoutExtensions.GridColumnsSizing);
	SetStringFieldIfPresent(NodeObj, TEXT("gridChildHorizontalAlign"), LayoutExtensions.GridChildHorizontalAlign);
	SetStringFieldIfPresent(NodeObj, TEXT("gridChildVerticalAlign"), LayoutExtensions.GridChildVerticalAlign);
	SetFloatFieldIfPresent(NodeObj, TEXT("counterAxisSpacing"), LayoutExtensions.CounterAxisSpacing, LayoutExtensions.bHasCounterAxisSpacing);
	SetFloatFieldIfPresent(NodeObj, TEXT("minWidth"), LayoutExtensions.MinWidth, LayoutExtensions.bHasMinWidth);
	SetFloatFieldIfPresent(NodeObj, TEXT("maxWidth"), LayoutExtensions.MaxWidth, LayoutExtensions.bHasMaxWidth);
	SetFloatFieldIfPresent(NodeObj, TEXT("minHeight"), LayoutExtensions.MinHeight, LayoutExtensions.bHasMinHeight);
	SetFloatFieldIfPresent(NodeObj, TEXT("maxHeight"), LayoutExtensions.MaxHeight, LayoutExtensions.bHasMaxHeight);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowCount"), LayoutExtensions.GridRowCount, LayoutExtensions.bHasGridRowCount);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnCount"), LayoutExtensions.GridColumnCount, LayoutExtensions.bHasGridColumnCount);
	SetFloatFieldIfPresent(NodeObj, TEXT("gridRowGap"), LayoutExtensions.GridRowGap, LayoutExtensions.bHasGridRowGap);
	SetFloatFieldIfPresent(NodeObj, TEXT("gridColumnGap"), LayoutExtensions.GridColumnGap, LayoutExtensions.bHasGridColumnGap);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowSpan"), LayoutExtensions.GridRowSpan, LayoutExtensions.bHasGridRowSpan);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnSpan"), LayoutExtensions.GridColumnSpan, LayoutExtensions.bHasGridColumnSpan);
	SetIntFieldIfPresent(NodeObj, TEXT("gridColumnAnchorIndex"), LayoutExtensions.GridColumnAnchorIndex, LayoutExtensions.bHasGridColumnAnchorIndex);
	SetIntFieldIfPresent(NodeObj, TEXT("gridRowAnchorIndex"), LayoutExtensions.GridRowAnchorIndex, LayoutExtensions.bHasGridRowAnchorIndex);
	SetBoolFieldIfPresent(NodeObj, TEXT("itemReverseZIndex"), LayoutExtensions.bItemReverseZIndex, LayoutExtensions.bHasItemReverseZIndex);
	SetBoolFieldIfPresent(NodeObj, TEXT("strokesIncludedInLayout"), LayoutExtensions.bStrokesIncludedInLayout, LayoutExtensions.bHasStrokesIncludedInLayout);
	SetJsonFieldIfPresent(NodeObj, TEXT("boundVariables"), LayoutExtensions.BoundVariablesJson);
	PromoteFigmaGridLayout(OutNode);

	OutNode.LayoutData.bHasPaddingRuntimeWrapDecision = true;
	if (OutNode.LayoutData.Layout == EDesignLayout::Horizontal || OutNode.LayoutData.Layout == EDesignLayout::Vertical)
	{
		OutNode.LayoutData.bPaddingRequiresRuntimeWrap = false;
	}
	else
	{
		OutNode.LayoutData.bPaddingRequiresRuntimeWrap =
			OutNode.LayoutData.Layout == EDesignLayout::None
			|| OutNode.LayoutData.Layout == EDesignLayout::Wrap
			|| OutNode.LayoutData.Layout == EDesignLayout::Grid;
	}
}

// ---------------------------------------------------------------------------
// ParseSize
// ---------------------------------------------------------------------------

void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	FNodeBounds VisualBounds;
	FNodeBounds LayoutBounds;
	const bool bHasVisualBounds = TryGetVisualBounds(NodeObj, ParentObj, VisualBounds);
	const bool bHasLayoutBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

	// Auto layout should remain in layout space; absolute layout uses visual bounds.
	const FNodeBounds& EffectiveBounds = bParentHasAutoLayout
		? (bHasLayoutBounds ? LayoutBounds : VisualBounds)
		: (bHasVisualBounds ? VisualBounds : LayoutBounds);

	float W = EffectiveBounds.W;
	float H = EffectiveBounds.H;

	if (bParentHasAutoLayout)
	{
		FString SizingH = GetString(NodeObj, TEXT("layoutSizingHorizontal"), TEXT("FIXED"));
		if (SizingH == TEXT("FILL"))
			OutNode.Transform.SetWidthValue(FDesignSize::Fill(W));
		else if (SizingH == TEXT("HUG"))
			OutNode.Transform.SetWidthValue(FDesignSize::Fit(W));
		else
			OutNode.Transform.SetWidthValue(FDesignSize::Fixed(W));

		FString SizingV = GetString(NodeObj, TEXT("layoutSizingVertical"), TEXT("FIXED"));
		if (SizingV == TEXT("FILL"))
			OutNode.Transform.SetHeightValue(FDesignSize::Fill(H));
		else if (SizingV == TEXT("HUG"))
			OutNode.Transform.SetHeightValue(FDesignSize::Fit(H));
		else
			OutNode.Transform.SetHeightValue(FDesignSize::Fixed(H));
	}
	else
	{
		OutNode.Transform.SetWidthValue(FDesignSize::Fixed(W));
		OutNode.Transform.SetHeightValue(FDesignSize::Fixed(H));
	}

	// Figma layoutGrow: 1.0 = fill, 0.0 = fixed (only meaningful in auto-layout)
	double GrowVal = 0.0;
	if (NodeObj->TryGetNumberField(TEXT("layoutGrow"), GrowVal))
		OutNode.LayoutData.LayoutGrow = static_cast<float>(GrowVal);

}

#include "FigmaParserStyling.h"

// ---------------------------------------------------------------------------
// ParsePosition
// ---------------------------------------------------------------------------

void ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.Transform.ClearPosition();
		return;
	}

	FNodeBounds ChildBounds;
	const bool bHasChildBounds = TryGetVisualBounds(NodeObj, ParentObj, ChildBounds);

	FNodeBounds ParentBounds;
	const bool bHasParentBounds = ParentObj && TryGetVisualBounds(ParentObj, nullptr, ParentBounds);

	if (bHasChildBounds && bHasParentBounds)
	{
		OutNode.Transform.SetPosition(ChildBounds.X - ParentBounds.X, ChildBounds.Y - ParentBounds.Y);
	}
	else if (bHasChildBounds)
	{
		OutNode.Transform.SetPosition(ChildBounds.X, ChildBounds.Y);
	}
}

// ---------------------------------------------------------------------------
// ParseNode (recursive)
// ---------------------------------------------------------------------------

static bool ShouldSkipParsedFigmaNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	FString& OutFigmaType)
{
	if (!NodeObj)
	{
		return true;
	}

	OutFigmaType = GetString(NodeObj, TEXT("type"));
	if (OutFigmaType.IsEmpty())
	{
		return true;
	}

	if (ShouldSkipFigmaNodeType(OutFigmaType))
	{
		if (SkippedDesignerNodes)
		{
			const FString Reason = (OutFigmaType == TEXT("SLICE"))
				? TEXT("design_export_only")
				: TEXT("figjam_only");
			AppendUniqueSkippedDesignerNode(
				*SkippedDesignerNodes,
				GetString(NodeObj, TEXT("id")),
				GetString(NodeObj, TEXT("name")),
				OutFigmaType,
				Reason);
		}
		UE_LOG(LogDesign2UMG, Verbose, TEXT("FigmaParser: skip non-runtime node type '%s' (%s)"),
			*OutFigmaType,
			*GetString(NodeObj, TEXT("name")));
		return true;
	}

	if (GetBool(NodeObj, TEXT("isMask"), false))
	{
		return true;
	}

	// Infer only the narrow, source-agnostic case: the first full-bounds leaf
	// shape inside a GROUP, with no strokes/effects and a single opaque solid fill.
	return IsLikelyImplicitMaskLeaf(NodeObj, ParentObj);
}

static void ApplyRelativeTransformFlipData(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node)
{
	const TArray<TSharedPtr<FJsonValue>>* TransformArr = GetArray(NodeObj, TEXT("relativeTransform"));
	if (!TransformArr || TransformArr->Num() != 2)
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>> Row0 = (*TransformArr)[0]->AsArray();
	const TArray<TSharedPtr<FJsonValue>> Row1 = (*TransformArr)[1]->AsArray();
	if (Row0.Num() != 3 || Row1.Num() != 3)
	{
		return;
	}

	const float A = Row0[0]->AsNumber();
	const float B = Row0[1]->AsNumber();
	const float C = Row1[0]->AsNumber();
	const float D = Row1[1]->AsNumber();
	const float Determinant = A * D - B * C;
	if (Determinant >= -KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float ScaleX = FMath::Sqrt(A * A + C * C);
	const float ScaleY = FMath::Sqrt(B * B + D * D);
	const float CosFromX = A / FMath::Max(ScaleX, KINDA_SMALL_NUMBER);
	const float CosFromY = D / FMath::Max(ScaleY, KINDA_SMALL_NUMBER);
	Node.Transform.bFlipX = (CosFromX * CosFromY < 0.0f) ? (CosFromX < 0.0f) : false;
	Node.Transform.bFlipY = (CosFromX * CosFromY < 0.0f) ? (CosFromY < 0.0f) : true;
}

static void InitializeNodeFromFigma(
	const TSharedPtr<FJsonObject>& NodeObj,
	const FString& FigmaType,
	bool bInsideInstanceSubtree,
	FDesignNode& OutNode)
{
	OutNode.Id = GetString(NodeObj, TEXT("id"));
	OutNode.Name = GetString(NodeObj, TEXT("name"));
	OutNode.Transform.bEnabled = GetBool(NodeObj, TEXT("visible"), true);
	OutNode.Transform.SetOpacityValue(GetNumber(NodeObj, TEXT("opacity"), 1.0));
	OutNode.Transform.Rotation = GetNumber(NodeObj, TEXT("rotation"));
	OutNode.MutableSource().ComponentInstance.bInInstanceSubtree = bInsideInstanceSubtree;
	OutNode.Type = MapNodeType(FigmaType);
	OutNode.MutableSource().SourceNodeType = FigmaType;
	OutNode.bReusable = (FigmaType == TEXT("COMPONENT"));
	OutNode.Metadata.Add(TEXT("source.provider"), TEXT("figma"));
	ApplyRelativeTransformFlipData(NodeObj, OutNode);
}

static void ApplyInstanceTrackingData(
	const TSharedPtr<FJsonObject>& NodeObj,
	const FString& FigmaType,
	FDesignNode& OutNode)
{
	if (FigmaType != TEXT("INSTANCE"))
	{
		return;
	}

	FDesignComponentInstanceData& ComponentInstance = OutNode.MutableSource().ComponentInstance;
	ComponentInstance.bIsInstance = true;
	ComponentInstance.bInInstanceSubtree = true;
	const FString ComponentId = GetString(NodeObj, TEXT("componentId"));
	if (!ComponentId.IsEmpty())
	{
		OutNode.Ref = ComponentId;
		ComponentInstance.ComponentId = ComponentId;
		OutNode.Metadata.Add(TEXT("instanceComponentId"), ComponentId);
	}
	SetStringFieldIfPresent(NodeObj, TEXT("componentSetId"), ComponentInstance.ComponentSetId);
	SetStringFieldIfPresent(NodeObj, TEXT("mainComponentKey"), ComponentInstance.MainComponentKey);
	SetJsonFieldIfPresent(NodeObj, TEXT("componentProperties"), ComponentInstance.ComponentPropertiesJson);
	SetJsonFieldIfPresent(NodeObj, TEXT("variantProperties"), ComponentInstance.VariantPropertiesJson);
	SetJsonFieldIfPresent(NodeObj, TEXT("overrides"), ComponentInstance.OverridesJson);
	SetBoolFieldIfPresent(NodeObj, TEXT("isExposedInstance"), ComponentInstance.bIsExposedInstance, ComponentInstance.bHasIsExposedInstance);
}

static void ParseNodeVisualData(
	const TSharedPtr<FJsonObject>& NodeObj,
	FDesignNode& OutNode)
{
	const TArray<TSharedPtr<FJsonValue>>* FillsArr = GetArray(NodeObj, TEXT("fills"));
	if (FillsArr)
	{
		OutNode.Visual.Fills = ParseFills(*FillsArr);
		OutNode.Visual.Fill = ParseFill(*FillsArr);
		const FString VectorRasterFallback = ExtractSyntheticVectorRasterImageRef(*FillsArr);
		if (!VectorRasterFallback.IsEmpty())
		{
			OutNode.MutableSource().VectorRasterFallbackImageRef = VectorRasterFallback;
		}
	}
	CaptureRenderFallbackMetadata(NodeObj, OutNode);
	CaptureBoundsDeltaMetadata(NodeObj, OutNode.Metadata);

	OutNode.Visual.Stroke = ParseStroke(NodeObj);
	if (OutNode.Type == EDesignNodeType::Path
		|| OutNode.Type == EDesignNodeType::Line
		|| OutNode.Type == EDesignNodeType::Polygon
		|| OutNode.Type == EDesignNodeType::Ellipse)
	{
		ParsePathGeometry(NodeObj, OutNode);
	}

	OutNode.Visual.CornerRadius = ParseCornerRadius(NodeObj);
	if (const TArray<TSharedPtr<FJsonValue>>* EffectsArr = GetArray(NodeObj, TEXT("effects")))
	{
		OutNode.Visual.Effects = ParseEffects(*EffectsArr);
	}
	if (OutNode.Type == EDesignNodeType::Text)
	{
		ParseTextProperties(NodeObj, OutNode);
	}
}

static void ParseNodeChildren(
	const TSharedPtr<FJsonObject>& NodeObj,
	const FString& FigmaType,
	bool bParentAutoLayout,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	FDesignNode& OutNode)
{
	if (FigmaType == TEXT("BOOLEAN_OPERATION") || FigmaType == TEXT("VECTOR"))
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ChildrenArr = GetArray(NodeObj, TEXT("children"));
	if (!ChildrenArr)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& ChildVal : *ChildrenArr)
	{
		const TSharedPtr<FJsonObject> ChildObj = ChildVal->AsObject();
		if (bParentAutoLayout && ShouldSkipAutoLayoutGhostChild(ChildObj, ChildrenArr))
		{
			continue;
		}

		if (TSharedPtr<FDesignNode> ChildNode = ParseNode(
			ChildObj,
			NodeObj,
			SkippedDesignerNodes,
			OutNode.Source().ComponentInstance.bIsInstance || OutNode.Source().ComponentInstance.bInInstanceSubtree))
		{
			OutNode.Children.Add(ChildNode);
		}
	}
}

static void FinalizeNodePaddingRuntimeWrap(FDesignNode& Node, bool bInsideInstanceSubtree)
{
	if (Node.LayoutData.Layout == EDesignLayout::Horizontal || Node.LayoutData.Layout == EDesignLayout::Vertical)
	{
		Node.LayoutData.bPaddingRequiresRuntimeWrap = ShouldFigmaAutoLayoutPaddingWrapAtRuntime(Node, bInsideInstanceSubtree);
	}
	else if (Node.LayoutData.Layout == EDesignLayout::Wrap || Node.LayoutData.Layout == EDesignLayout::Grid)
	{
		Node.LayoutData.bPaddingRequiresRuntimeWrap = Node.Children.Num() > 0;
	}
}

TSharedPtr<FDesignNode> ParseNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	bool bInsideInstanceSubtree)
{
	FString FigmaType;
	if (ShouldSkipParsedFigmaNode(NodeObj, ParentObj, SkippedDesignerNodes, FigmaType))
	{
		return nullptr;
	}

	auto Node = MakeShared<FDesignNode>();
	InitializeNodeFromFigma(NodeObj, FigmaType, bInsideInstanceSubtree, *Node);
	ApplyInstanceTrackingData(NodeObj, FigmaType, *Node);

	const bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;

	// Position
	ParsePosition(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Size
	ParseSize(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);
	ParsePrototypeNodeData(NodeObj, Node->MutableSource().Prototype);
	ParseNodeVisualData(NodeObj, *Node);
	ParseNodeChildren(NodeObj, FigmaType, bParentAutoLayout, SkippedDesignerNodes, *Node);

	ApplyFigmaSemanticHints(NodeObj, *Node);
	FinalizeNodePaddingRuntimeWrap(*Node, bInsideInstanceSubtree);

	return Node;
}

TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	return ParseNode(NodeObj, ParentObj, nullptr, false);
}

static FString SerializeSkippedDesignerNodes(const TArray<FDesignSkippedNodeReport>& Reports)
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(Reports.Num());
	for (const FDesignSkippedNodeReport& Report : Reports)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!Report.NodeId.IsEmpty())
		{
			Entry->SetStringField(TEXT("id"), Report.NodeId);
		}
		if (!Report.NodeName.IsEmpty())
		{
			Entry->SetStringField(TEXT("name"), Report.NodeName);
		}
		if (!Report.NodeType.IsEmpty())
		{
			Entry->SetStringField(TEXT("type"), Report.NodeType);
		}
		if (!Report.Reason.IsEmpty())
		{
			Entry->SetStringField(TEXT("reason"), Report.Reason);
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Entries, Writer);
	return Json;
}

// ---------------------------------------------------------------------------
// ParseDocument
// ---------------------------------------------------------------------------

bool ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	auto Root = ParseJsonString(FigmaJson);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: Failed to parse JSON"));
		return false;
	}

	auto DocObj = GetObject(Root, TEXT("document"));
	if (!DocObj)
	{
		// Fallback: Figma nodes API payload (e.g. /v1/files/{key}/nodes?...),
		// structure is usually Root.nodes["node-id"].document.
		const TSharedPtr<FJsonObject> NodesObj = GetObject(Root, TEXT("nodes"));
		if (NodesObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : NodesObj->Values)
			{
				const TSharedPtr<FJsonObject> NodeEntry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
				if (!NodeEntry.IsValid())
					continue;

				DocObj = GetObject(NodeEntry, TEXT("document"));
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

	const FString DocType = GetString(DocObj, TEXT("type"));

	auto CaptureDocumentMetadata = [&OutDocument](const TSharedPtr<FJsonObject>& SourceObj)
	{
		CaptureSharedMetadataExtensions(SourceObj, OutDocument.Metadata);
	};
	CaptureDocumentMetadata(DocObj);
	CaptureDocumentMetadata(Root);
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("components"), TEXT("components"));
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("componentSets"), TEXT("componentSets"));
	SetJsonFieldIfPresent(Root, TEXT("components"), OutDocument.MutableSourceDocument().ComponentsJson);
	SetJsonFieldIfPresent(Root, TEXT("componentSets"), OutDocument.MutableSourceDocument().ComponentSetsJson);
	MergePrototypeDocumentData(DocObj, OutDocument.MutableSourceDocument());
	MergePrototypeDocumentData(Root, OutDocument.MutableSourceDocument());

	// Standard file payload: DOCUMENT -> pages -> frames.
	if (DocType == TEXT("DOCUMENT"))
	{
		const auto* Pages = GetArray(DocObj, TEXT("children"));
		if (!Pages)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: No pages found"));
			return true;
		}

	for (const auto& PageVal : *Pages)
		{
			auto PageObj = PageVal->AsObject();
			if (!PageObj) continue;

			const auto* Frames = GetArray(PageObj, TEXT("children"));
			if (!Frames) continue;

			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false))
					OutDocument.Children.Add(Node);
			}
		}
	}
	// Nodes API may directly return FRAME / CANVAS / COMPONENT roots.
	else if (DocType == TEXT("CANVAS"))
	{
		const auto* Frames = GetArray(DocObj, TEXT("children"));
		if (Frames)
		{
			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false))
					OutDocument.Children.Add(Node);
			}
		}
	}
	else
	{
		if (auto Node = ParseNode(DocObj, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false))
			OutDocument.Children.Add(Node);
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaParser: Parsed %d top-level frames"), OutDocument.Children.Num());
	return true;
}

// ---------------------------------------------------------------------------
// ExtractFileKey
// ---------------------------------------------------------------------------

bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey)
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

void CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs)
{
	CollectImageRefsRecursive(Document.Children, OutImageRefs);
}

void CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		// Primary fill
		if (Node->Visual.Fill.IsImage() && !Node->Visual.Fill.Image.Url.IsEmpty())
			OutRefs.Add(Node->Visual.Fill.Image.Url);
		// Multi-fill layers
		for (const FDesignFill& F : Node->Visual.Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
				OutRefs.Add(F.Image.Url);
		}
		if (!Node->Source().VectorRasterFallbackImageRef.IsEmpty())
			OutRefs.Add(Node->Source().VectorRasterFallbackImageRef);
		if (!Node->Source().RenderFallback.ImageRef.IsEmpty())
			OutRefs.Add(Node->Source().RenderFallback.ImageRef);
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
}

void ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		// Primary fill
		if (Node->Visual.Fill.IsImage() && !Node->Visual.Fill.Image.Url.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Visual.Fill.Image.Url))
				Node->Visual.Fill.Image.Url = *Url;
		}
		// Multi-fill layers
		for (FDesignFill& F : Node->Visual.Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
			{
				if (const FString* Url = Map.Find(F.Image.Url))
					F.Image.Url = *Url;
			}
		}
		if (!Node->Source().VectorRasterFallbackImageRef.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Source().VectorRasterFallbackImageRef))
			{
				Node->MutableSource().VectorRasterFallbackImageRef = *Url;
			}
		}
		if (!Node->Source().RenderFallback.ImageRef.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Source().RenderFallback.ImageRef))
			{
				Node->MutableSource().RenderFallback.ImageRef = *Url;
			}
		}
		ApplyImageUrlsRecursive(Node->Children, Map);
	}
}





}
