// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserInternal.h"
#include "DesignComponentMapping.h"
#include "DesignTreeLimits.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserLayout.h"
#include "FigmaParserLayoutBounds.h"
#include "FigmaParserPrototype.h"
#include "FigmaParserMetadata.h"
#include "FigmaParserStyling.h"
#include "FigmaParserText.h"
#include "ParserSharedSemantics.h"
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

void Private::SetStringFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& OutValue)
{
	if (!Obj.IsValid())
	{
		return;
	}

	const FString Value = GetString(Obj, Key);
	if (!Value.IsEmpty())
	{
		OutValue = Value;
	}
}

static void SetBoolFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TOptional<bool>& Out)
{
	if (!Obj.IsValid())
		return;
	bool Value = false;
	if (Obj->TryGetBoolField(Key, Value))
		Out.Emplace(Value);
}

static void SetJsonFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& OutValue)
{
	if (!Obj.IsValid())
		return;

	const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(Key);
	if (FieldValue.IsValid())
	{
		OutValue = DesignJsonUtils::ToPrettyString(FieldValue);
	}
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
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		const FString ReaderError = Reader->GetErrorMessage();
		if (ReaderError.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Invalid Figma export JSON. Please verify the export file is a valid JSON object."));
		}
		else
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Invalid Figma export JSON. Please verify the export file is a valid JSON object. Reader error: %s"), *ReaderError);
		}
		return nullptr;
	}
	return Root;
}

static bool HasSingleOpaqueVisibleSolidFill(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
	if (!Fills)
	{
		return false;
	}

	TSharedPtr<FJsonObject> VisibleFill;
	for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
	{
		const TSharedPtr<FJsonObject> Fill = FillValue->AsObject();
		if (!Fill.IsValid() || !GetBool(Fill, TEXT("visible"), true))
		{
			continue;
		}

		if (VisibleFill.IsValid())
		{
			return false;
		}
		VisibleFill = Fill;
	}

	if (!VisibleFill.IsValid() || GetString(VisibleFill, TEXT("type")) != TEXT("SOLID"))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Color = GetObject(VisibleFill, TEXT("color"));
	return Color.IsValid() && GetNumber(Color, TEXT("a"), 1.0) >= 0.99;
}

static bool IsGraphicalLeaf(const TSharedPtr<FJsonObject>& NodeObj)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	if (Children && Children->Num() > 0)
	{
		return false;
	}

	const FString Type = GetString(NodeObj, TEXT("type"));
	return Type == TEXT("RECTANGLE")
		|| Type == TEXT("ELLIPSE")
		|| Type == TEXT("LINE")
		|| Type == TEXT("REGULAR_POLYGON")
		|| Type == TEXT("POLYGON")
		|| Type == TEXT("STAR")
		|| Type == TEXT("VECTOR")
		|| Type == TEXT("BOOLEAN_OPERATION");
}

static bool IsGraphicalMaskContentSubtree(const TSharedPtr<FJsonObject>& NodeObj, const int32 Depth = 0)
{
	if (Depth > DesignTreeLimits::MaxNodeDepth)
	{
		return false;
	}
	if (IsGraphicalLeaf(NodeObj))
	{
		return true;
	}
	if (!NodeObj.IsValid())
	{
		return false;
	}

	const FString Type = GetString(NodeObj, TEXT("type"));
	if (Type != TEXT("GROUP") && Type != TEXT("TRANSFORM_GROUP") && Type != TEXT("INSTANCE"))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	if (!Children || Children->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Child : *Children)
	{
		if (!IsGraphicalMaskContentSubtree(Child->AsObject(), Depth + 1))
		{
			return false;
		}
	}
	return true;
}

static bool HasNearlyIdenticalBoundingBox(
	const TSharedPtr<FJsonObject>& A,
	const TSharedPtr<FJsonObject>& B);

static bool DoesGraphicalSubtreeEscapeBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& BoundsOwner,
	const int32 Depth = 0)
{
	if (!NodeObj.IsValid() || !BoundsOwner.IsValid() || Depth > DesignTreeLimits::MaxNodeDepth)
	{
		return false;
	}

	if (IsGraphicalLeaf(NodeObj))
	{
		const TSharedPtr<FJsonObject> NodeBounds = GetObject(NodeObj, TEXT("absoluteBoundingBox"));
		const TSharedPtr<FJsonObject> ClipBounds = GetObject(BoundsOwner, TEXT("absoluteBoundingBox"));
		if (!NodeBounds.IsValid() || !ClipBounds.IsValid())
		{
			return false;
		}

		const double NodeX = GetNumber(NodeBounds, TEXT("x"));
		const double NodeY = GetNumber(NodeBounds, TEXT("y"));
		const double NodeRight = NodeX + GetNumber(NodeBounds, TEXT("width"));
		const double NodeBottom = NodeY + GetNumber(NodeBounds, TEXT("height"));
		const double ClipX = GetNumber(ClipBounds, TEXT("x"));
		const double ClipY = GetNumber(ClipBounds, TEXT("y"));
		const double ClipRight = ClipX + GetNumber(ClipBounds, TEXT("width"));
		const double ClipBottom = ClipY + GetNumber(ClipBounds, TEXT("height"));
		return NodeX < ClipX - 0.5
			|| NodeY < ClipY - 0.5
			|| NodeRight > ClipRight + 0.5
			|| NodeBottom > ClipBottom + 0.5;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	if (!Children)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Child : *Children)
	{
		if (DoesGraphicalSubtreeEscapeBounds(Child->AsObject(), BoundsOwner, Depth + 1))
		{
			return true;
		}
	}
	return false;
}

static bool IsImplicitGroupMaskStackStart(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj.IsValid()
		|| !ParentObj.IsValid()
		|| GetString(ParentObj, TEXT("type")) != TEXT("GROUP")
		|| !IsGraphicalLeaf(NodeObj)
		|| HasVisibleStroke(NodeObj)
		|| HasVisibleEffect(NodeObj)
		|| !HasSingleOpaqueVisibleSolidFill(NodeObj)
		|| !HasNearlyIdenticalBoundingBox(NodeObj, ParentObj))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(ParentObj, TEXT("children"));
	if (!Children || Children->Num() <= 1 || (*Children)[0]->AsObject() != NodeObj)
	{
		return false;
	}

	bool bRequiresMaskClip = false;
	for (int32 Index = 1; Index < Children->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> ContentNode = (*Children)[Index]->AsObject();
		if (!IsGraphicalMaskContentSubtree(ContentNode))
		{
			return false;
		}
		bRequiresMaskClip |= DoesGraphicalSubtreeEscapeBounds(ContentNode, NodeObj);
	}

	return bRequiresMaskClip;
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

static TOptional<TArray<FDesignGeometryPath>> ParseGeometryArray(
	const TArray<TSharedPtr<FJsonValue>>* GeometryArr)
{
	if (!GeometryArr)
		return {};

	TArray<FDesignGeometryPath> Paths;
	for (const TSharedPtr<FJsonValue>& GeometryVal : *GeometryArr)
	{
		const TSharedPtr<FJsonObject> GeometryObj = GeometryVal->AsObject();
		if (!GeometryObj.IsValid())
			continue;
		const FString Path = GetString(GeometryObj, TEXT("path"));
		if (Path.IsEmpty())
			continue;
		FDesignGeometryPath& Geometry = Paths.AddDefaulted_GetRef();
		Geometry.Path = Path;
		Geometry.FillRule = NormalizeWindingRule(GetString(GeometryObj, TEXT("windingRule")));
	}
	return MoveTemp(Paths);
}

EDesignNodeType MapNodeType(const FString& FigmaType);
TSharedPtr<FDesignNode> ParseNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds* ParentCoordinateBounds,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	bool bInsideInstanceSubtree,
	FDesignParserBudget& ParseBudget,
	int32 Depth,
	bool bParentTransformedCoordinates = false);
void CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs);
void ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map);

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

static void ParsePathGeometry(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	if (OutNode.Type == EDesignNodeType::Ellipse)
	{
		if (const TSharedPtr<FJsonObject> Arc = GetObject(NodeObj, TEXT("arcData")))
		{
			const double Start = GetNumber(Arc, TEXT("startingAngle"));
			const double End = GetNumber(Arc, TEXT("endingAngle"));
			OutNode.Shape.InnerRadius = GetNumber(Arc, TEXT("innerRadius"));
			// The shared shape convention is counterclockwise degrees; Figma uses clockwise radians.
			OutNode.Shape.StartAngle = -FMath::RadiansToDegrees(Start);
			OutNode.Shape.SweepAngle = FMath::RadiansToDegrees(Start - End);
		}
	}
	FDesignPaintGeometry& Geometry = OutNode.Shape.PaintGeometry;
	Geometry.FillPaths = ParseGeometryArray(GetArray(NodeObj, TEXT("fillGeometry")));
	Geometry.StrokePaths = ParseGeometryArray(GetArray(NodeObj, TEXT("strokeGeometry")));

	if (OutNode.Type == EDesignNodeType::Path || OutNode.Type == EDesignNodeType::Line
		|| OutNode.Type == EDesignNodeType::Polygon || OutNode.Type == EDesignNodeType::Ellipse)
	{
		const auto* Paths = Geometry.FillPaths.IsSet() ? &Geometry.FillPaths.GetValue() : nullptr;
		if (!Paths || Paths->IsEmpty())
			Paths = Geometry.StrokePaths.IsSet() ? &Geometry.StrokePaths.GetValue() : nullptr;
		if (Paths && !Paths->IsEmpty())
		{
			OutNode.Shape.Geometry = (*Paths)[0].Path;
			OutNode.Shape.FillRule = (*Paths)[0].FillRule;
			const TSharedPtr<FJsonObject> Size = GetObject(NodeObj, TEXT("size"));
			double Width = 0.0;
			double Height = 0.0;
			if (Size.IsValid() && Size->TryGetNumberField(TEXT("x"), Width)
				&& Size->TryGetNumberField(TEXT("y"), Height)
				&& FMath::IsFinite(Width) && FMath::IsFinite(Height) && Width > 0.0 && Height > 0.0)
			{
				// Figma paths use local size, not the transformed absolute bounding box.
				OutNode.Shape.ViewBox = FVector4(0.0, 0.0, Width, Height);
			}
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

	return IsImplicitGroupMaskStackStart(NodeObj, ParentObj);
}

static void ParseExplicitGeometryMask(
	const TSharedPtr<FJsonObject>& NodeObj,
	FDesignNode& OutNode)
{
	const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
	if (!Children || Children->Num() == 0)
	{
		return;
	}

	const TSharedPtr<FJsonObject> MaskNode = (*Children)[0]->AsObject();
	if (!MaskNode.IsValid()
		|| !GetBool(MaskNode, TEXT("isMask"), false)
		|| !GetBool(MaskNode, TEXT("visible"), true)
		|| !FMath::IsNearlyZero(GetNumber(MaskNode, TEXT("rotation")))
		|| !HasNearlyIdenticalBoundingBox(MaskNode, NodeObj))
	{
		return;
	}

	const FString MaskType = GetString(MaskNode, TEXT("type"));
	if (MaskType == TEXT("ELLIPSE"))
	{
		OutNode.Visual.GeometryMask.Shape = EDesignGeometryMaskShape::Ellipse;
		return;
	}

	if (MaskType == TEXT("RECTANGLE"))
	{
		const FDesignCornerRadius CornerRadius = ParseCornerRadius(MaskNode);
		if (CornerRadius.HasRadius())
		{
			OutNode.Visual.GeometryMask.Shape = EDesignGeometryMaskShape::RoundedRectangle;
			OutNode.Visual.GeometryMask.CornerRadius = CornerRadius;
		}
	}
}

static void ApplyRelativeTransform(const FRelativeTransform& Transform, FDesignNode& Node)
{
	const double Determinant = Transform.A * Transform.D - Transform.B * Transform.C;
	if (Determinant < 0.0f)
	{
		Node.Transform.bFlipX = Transform.A < 0.0;
		Node.Transform.bFlipY = !Node.Transform.bFlipX;
	}
	Node.Transform.Rotation = FMath::RadiansToDegrees(FMath::Atan2(
		Node.Transform.bFlipX ? -Transform.C : Transform.C,
		Node.Transform.bFlipX ? -Transform.A : Transform.A));
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
	OutNode.MutableSource().ComponentInstance.bInInstanceSubtree = bInsideInstanceSubtree;
	OutNode.Type = MapNodeType(FigmaType);
	OutNode.MutableSource().SourceNodeType = FigmaType;
	OutNode.bReusable = (FigmaType == TEXT("COMPONENT"));
	OutNode.Metadata.Add(TEXT("source.provider"), DesignComponentMapping::ProviderFigma);
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
	Private::SetStringFieldIfPresent(NodeObj, TEXT("componentSetId"), ComponentInstance.ComponentSetId);
	Private::SetStringFieldIfPresent(NodeObj, TEXT("componentSetKey"), ComponentInstance.ComponentSetKey);
	Private::SetStringFieldIfPresent(NodeObj, TEXT("mainComponentKey"), ComponentInstance.MainComponentKey);
	SetJsonFieldIfPresent(NodeObj, TEXT("componentProperties"), ComponentInstance.ComponentPropertiesJson);
	SetJsonFieldIfPresent(NodeObj, TEXT("variantProperties"), ComponentInstance.VariantPropertiesJson);
	SetJsonFieldIfPresent(NodeObj, TEXT("overrides"), ComponentInstance.OverridesJson);
	SetBoolFieldIfPresent(NodeObj, TEXT("isExposedInstance"), ComponentInstance.IsExposedInstance);
}

static void CaptureFigmaStrokeMetadata(
	const TSharedPtr<FJsonObject>& NodeObj,
	FDesignNode& OutNode)
{
	const FString StrokeCap = GetString(NodeObj, TEXT("strokeCap"));
	if (!StrokeCap.IsEmpty())
	{
		OutNode.Metadata.Add(ParserSharedSemantics::StrokeCapMetadataKey, StrokeCap);
		EDesignStrokeCap ParsedCap;
		if (ParserSharedSemantics::TryParseStrokeCapToken(StrokeCap, ParsedCap))
		{
			OutNode.Visual.Stroke.Cap = ParsedCap;
		}
	}

	const FString StrokeJoin = GetString(NodeObj, TEXT("strokeJoin"));
	if (!StrokeJoin.IsEmpty())
	{
		OutNode.Metadata.Add(ParserSharedSemantics::StrokeJoinMetadataKey, StrokeJoin);
		EDesignStrokeJoin ParsedJoin;
		if (ParserSharedSemantics::TryParseStrokeJoinToken(StrokeJoin, ParsedJoin))
		{
			OutNode.Visual.Stroke.Join = ParsedJoin;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* StrokeDashes = GetArray(NodeObj, TEXT("strokeDashes"));
	if (!StrokeDashes || StrokeDashes->Num() == 0)
	{
		return;
	}

	FString SerializedDashes;
	for (const TSharedPtr<FJsonValue>& DashValue : *StrokeDashes)
	{
		if (!DashValue.IsValid() || DashValue->Type != EJson::Number)
		{
			continue;
		}

		const float DashLength = static_cast<float>(DashValue->AsNumber());
		if (FMath::IsFinite(DashLength) && DashLength > KINDA_SMALL_NUMBER)
		{
			OutNode.Visual.Stroke.DashPattern.Add(DashLength);
		}

		if (!SerializedDashes.IsEmpty())
		{
			SerializedDashes += TEXT(",");
		}
		SerializedDashes += FString::SanitizeFloat(DashValue->AsNumber());
	}

	if (!SerializedDashes.IsEmpty())
	{
		OutNode.Metadata.Add(TEXT("stroke.dashes"), SerializedDashes);
	}
}

static void ParseNodeVisualData(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	FDesignNode& OutNode)
{
	const TArray<TSharedPtr<FJsonValue>>* FillsArr = GetArray(NodeObj, TEXT("fills"));
	if (FillsArr)
	{
		OutNode.Visual.Fills = ParseFills(*FillsArr);
		OutNode.Visual.bFillExplicitClear = FillsArr->Num() == 0;
		for (const TSharedPtr<FJsonValue>& FillValue : *FillsArr)
		{
			const TSharedPtr<FJsonObject> Fill = FillValue.IsValid() && FillValue->Type == EJson::Object ? FillValue->AsObject() : nullptr;
			if (!Fill.IsValid() || GetString(Fill, TEXT("type")) != TEXT("IMAGE") || !GetBool(Fill, TEXT("visible"), true)) continue;
			if (!GetString(Fill, TEXT("gifRef")).IsEmpty()) OutNode.Metadata.Add(TEXT("figma.gifStaticFrame"), TEXT("true"));
			const TSharedPtr<FJsonObject> Filters = GetObject(Fill, TEXT("filters"));
			if (!Filters.IsValid()) continue;
			for (const auto& Filter : Filters->Values)
			{
				if (Filter.Value.IsValid() && Filter.Value->Type == EJson::Number && Filter.Value->AsNumber() != 0.0)
				{
					OutNode.Metadata.Add(TEXT("figma.imageFiltersUnsupported"), TEXT("true"));
					break;
				}
			}
		}
	}
	CaptureBoundsDeltaMetadata(NodeObj, OutNode.Metadata);

	OutNode.Visual.Stroke = ParseStroke(NodeObj);
	CaptureFigmaStrokeMetadata(NodeObj, OutNode);
	ParsePathGeometry(NodeObj, OutNode);

	OutNode.Visual.CornerRadius = ParseCornerRadius(NodeObj);
	ParseExplicitGeometryMask(NodeObj, OutNode);
	if (const TArray<TSharedPtr<FJsonValue>>* EffectsArr = GetArray(NodeObj, TEXT("effects")))
	{
		OutNode.Visual.Effects = ParseEffects(*EffectsArr);
		OutNode.Visual.bEffectsExplicitClear = EffectsArr->Num() == 0;
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
	const FNodeBounds* NodeCoordinateBounds,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	FDesignParserBudget& ParseBudget,
	int32 Depth,
	bool bTransformedCoordinates,
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
			NodeCoordinateBounds,
			SkippedDesignerNodes,
			OutNode.Source().ComponentInstance.bIsInstance || OutNode.Source().ComponentInstance.bInInstanceSubtree,
			ParseBudget,
			Depth + 1,
			bTransformedCoordinates))
		{
			OutNode.Children.Add(ChildNode);
		}
	}
}

TSharedPtr<FDesignNode> ParseNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds* ParentCoordinateBounds,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	bool bInsideInstanceSubtree,
	FDesignParserBudget& ParseBudget,
	int32 Depth,
	bool bParentTransformedCoordinates)
{
	if (!ParseBudget.TryEnterNode(Depth))
	{
		return nullptr;
	}

	FString FigmaType;
	if (ShouldSkipParsedFigmaNode(NodeObj, ParentObj, SkippedDesignerNodes, FigmaType))
	{
		return nullptr;
	}

	auto Node = MakeShared<FDesignNode>();
	InitializeNodeFromFigma(NodeObj, FigmaType, bInsideInstanceSubtree, *Node);
	ApplyInstanceTrackingData(NodeObj, FigmaType, *Node);
	FLocalGeometry LocalGeometry;
	const bool bHasLocalGeometry = TryGetLocalGeometry(NodeObj, LocalGeometry);
	if (bHasLocalGeometry)
	{
		ApplyRelativeTransform(LocalGeometry.Transform, *Node);
	}
	else
	{
		FRelativeTransform SourceTransform;
		const bool bHasSourceTransform = TryGetRelativeTransform(NodeObj, SourceTransform);
		if (!FMath::IsNearlyZero(GetNumber(NodeObj, TEXT("rotation")))
			|| (bHasSourceTransform && (!FMath::IsNearlyEqual(SourceTransform.A, 1.0)
				|| !FMath::IsNearlyEqual(SourceTransform.D, 1.0)
				|| !FMath::IsNearlyZero(SourceTransform.B) || !FMath::IsNearlyZero(SourceTransform.C))))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: transform not applied because local matrix or size is missing (%s)"), *Node->Id);
		}
	}

	const bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;
	const bool bTransformedCoordinates = bParentTransformedCoordinates || !FMath::IsNearlyZero(Node->Transform.Rotation)
		|| Node->Transform.bFlipX || Node->Transform.bFlipY;
	const auto* FillGeometry = GetArray(NodeObj, TEXT("fillGeometry"));
	const auto* StrokeGeometry = GetArray(NodeObj, TEXT("strokeGeometry"));
	const auto* Fills = GetArray(NodeObj, TEXT("fills"));
	const bool bSourceStrokeOnly = FillGeometry && FillGeometry->IsEmpty() && Fills && Fills->IsEmpty()
		&& StrokeGeometry && !StrokeGeometry->IsEmpty();
	const FLocalGeometry* ActiveLocalGeometry = bHasLocalGeometry && (bTransformedCoordinates || bSourceStrokeOnly)
		? &LocalGeometry : nullptr;

	// Position
	ParsePosition(NodeObj, ParentObj, ParentCoordinateBounds, *Node, bParentAutoLayout, bParentTransformedCoordinates, ActiveLocalGeometry);

	// Size
	ParseSize(NodeObj, ParentObj, *Node, bParentAutoLayout, ActiveLocalGeometry);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);
	ParsePrototypeNodeData(NodeObj, Node->MutableSource().Prototype);
	ParseNodeVisualData(NodeObj, ParentObj, *Node);
	FNodeBounds NodeCoordinateBounds;
	const bool bHasNodeCoordinateBounds = bParentAutoLayout
		? TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), NodeCoordinateBounds)
		: TryGetVisualBounds(NodeObj, ParentObj, NodeCoordinateBounds);
	Node->LayoutData.bChildPositionsRelativeToVisualBounds =
		!bParentAutoLayout && bHasNodeCoordinateBounds;
	ParseNodeChildren(
		NodeObj,
		FigmaType,
		bParentAutoLayout,
		bHasNodeCoordinateBounds ? &NodeCoordinateBounds : nullptr,
		SkippedDesignerNodes,
		ParseBudget,
		Depth,
		bTransformedCoordinates,
		*Node);

	if (!ParentObj && bHasNodeCoordinateBounds)
	{
		FNodeBounds LayoutBounds;
		if (TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds))
		{
			// Local transforms place the layout center at the source bounding-box center.
			const FVector2D Origin = ActiveLocalGeometry
				? FVector2D(LayoutBounds.X + (LayoutBounds.W - ActiveLocalGeometry->Size.X) * 0.5,
					LayoutBounds.Y + (LayoutBounds.H - ActiveLocalGeometry->Size.Y) * 0.5)
				: FVector2D(NodeCoordinateBounds.X, NodeCoordinateBounds.Y);
			Node->MutableSource().ExportBounds = FigmaParserMetadata::CalculateExportBounds(*Node, Origin);
		}
	}

	FigmaParserMetadata::ApplyFigmaSemanticHints(NodeObj, *Node);
	FinalizeNodePaddingRuntimeWrap(*Node);

	return Node;
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
	FDesignParserBudget ParseBudget;
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
			for (const auto& Pair : NodesObj->Values)
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
		FigmaParserMetadata::CaptureSharedMetadataExtensions(SourceObj, OutDocument.Metadata);
	};
	CaptureDocumentMetadata(DocObj);
	CaptureDocumentMetadata(Root);
	// DocumentScope prefers the file key: the export chain captures it when present at the top level, for Component Mapping use.
	const FString FileKeyValue = GetString(Root, TEXT("fileKey"));
	if (!FileKeyValue.IsEmpty())
	{
		OutDocument.Metadata.Add(TEXT("fileKey"), FileKeyValue);
	}
	// Document-level provider marker: component identity extraction and Component Mapping use it to distinguish Figma from Pencil.
	OutDocument.Metadata.Add(TEXT("source.provider"), DesignComponentMapping::ProviderFigma);
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("components"), TEXT("components"));
	AddJsonExtensionIfPresent(Root, OutDocument.Metadata, TEXT("componentSets"), TEXT("componentSets"));
	SetJsonFieldIfPresent(Root, TEXT("components"), OutDocument.MutableSourceDocument().ComponentsJson);
	SetJsonFieldIfPresent(Root, TEXT("componentSets"), OutDocument.MutableSourceDocument().ComponentSetsJson);
	SetJsonFieldIfPresent(Root, TEXT("design2umgComponentDependencies"), OutDocument.MutableSourceDocument().ComponentDependenciesJson);
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
				if (auto Node = ParseNode(FrameObj, nullptr, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false, ParseBudget, 0))
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
				if (auto Node = ParseNode(FrameObj, nullptr, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false, ParseBudget, 0))
					OutDocument.Children.Add(Node);
			}
		}
	}
	else
	{
		if (auto Node = ParseNode(DocObj, nullptr, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false, ParseBudget, 0))
			OutDocument.Children.Add(Node);
	}

	if (Root->HasField(TEXT("design2umgPrototypePages")))
	{
		const auto* PrototypePages = GetArray(Root, TEXT("design2umgPrototypePages"));
		if (!PrototypePages)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: prototype pages must be an array"));
			return false;
		}
		for (const auto& PageValue : *PrototypePages)
		{
			const auto Page = PageValue.IsValid() && PageValue->Type == EJson::Object ? PageValue->AsObject() : nullptr;
			if (!Page)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: prototype page must be an object"));
				return false;
			}
			if (auto Node = ParseNode(Page, nullptr, nullptr, &OutDocument.MutableParseDiagnostics().SkippedDesignerNodes, false, ParseBudget, 0))
				OutDocument.MutableSourceDocument().PrototypePages.Add(Node);
		}
	}

	if (ParseBudget.WasExceeded())
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("FigmaParser: node tree exceeds the supported limits (%d levels or %d nodes)"),
			DesignTreeLimits::MaxNodeDepth,
			DesignTreeLimits::MaxParsedNodeCount);
		OutDocument = FDesignDocument();
		return false;
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
	CollectImageRefsRecursive(Document.SourceDocument().PrototypePages, OutImageRefs);
}

void CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		for (const FDesignFill& F : Node->Visual.Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
				OutRefs.Add(F.Image.Url);
		}
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	if (!Document.SourceDocument().ComponentDependenciesJson.IsEmpty())
	{
		Document.MutableSourceDocument().ResolvedComponentImageUrls.Append(ImageRefToUrl);
	}
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
	ApplyImageUrlsRecursive(Document.MutableSourceDocument().PrototypePages, ImageRefToUrl);
}

void ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		for (FDesignFill& F : Node->Visual.Fills)
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

}

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	OutDocument = FDesignDocument();
	FDesignDocument ParsedDocument;
	if (!FigmaParserInternal::ParseDocument(FigmaJson, ParsedDocument)) return false;
	OutDocument = MoveTemp(ParsedDocument);
	return true;
}

bool FFigmaParser::ExtractFileKey(const FString& UrlOrKey, FString& OutKey)
{
	return FigmaParserInternal::ExtractFileKey(UrlOrKey, OutKey);
}

void FFigmaParser::CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs)
{
	FigmaParserInternal::CollectImageRefs(Document, OutImageRefs);
}

void FFigmaParser::ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	FigmaParserInternal::ApplyImageUrls(Document, ImageRefToUrl);
}
