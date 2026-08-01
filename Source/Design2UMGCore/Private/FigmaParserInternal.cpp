// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserInternal.h"
#include "Design2UMGSourceMetadata.h"
#include "DesignTreeLimits.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserLayout.h"
#include "FigmaParserLayoutBounds.h"
#include "FigmaParserPrototype.h"
#include "FigmaParserMetadata.h"
#include "FigmaParserStyling.h"
#include "FigmaParserText.h"
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

static void AddJsonExtensionIfPresent(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions, const FString& SourceKey, const FString& ExtKey)
{
	if (!Obj.IsValid())
		return;
	const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(SourceKey);
	if (!FieldValue.IsValid())
		return;
	Extensions.Add(ExtKey, DesignJsonUtils::ToCompactString(FieldValue));
}


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
		OutValue = DesignJsonUtils::ToCompactString(FieldValue);
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

EDesignNodeType MapNodeType(const FString& FigmaType);
TSharedPtr<FDesignNode> ParseNode(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	const FNodeBounds* ParentCoordinateBounds,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	bool bInsideInstanceSubtree,
	FDesignParserBudget& ParseBudget,
	int32 Depth);
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
	Private::SetStringFieldIfPresent(NodeObj, TEXT("componentSetId"), ComponentInstance.ComponentSetId);
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

		if (!SerializedDashes.IsEmpty())
		{
			SerializedDashes += TEXT(",");
		}
		SerializedDashes += FString::SanitizeFloat(DashValue->AsNumber());
	}

	if (!SerializedDashes.IsEmpty())
	{
		OutNode.Metadata.Add(TEXT("stroke.dashes"), SerializedDashes);
		const FString StrokeCap = GetString(NodeObj, TEXT("strokeCap"));
		if (!StrokeCap.IsEmpty())
		{
			OutNode.Metadata.Add(TEXT("stroke.cap"), StrokeCap);
		}
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
		OutNode.Visual.Fill = ParseFill(*FillsArr);
		const FString VectorRasterFallback = ExtractSyntheticVectorRasterImageRef(*FillsArr);
		if (!VectorRasterFallback.IsEmpty())
		{
			OutNode.MutableSource().VectorRasterFallbackImageRef = VectorRasterFallback;
			if (HasParentClippedRenderBounds(NodeObj, ParentObj))
			{
				OutNode.Metadata.Add(Design2UMGSourceMetadata::RenderBoundsClippedByParentKey, TEXT("true"));
			}
		}
	}
	FigmaParserMetadata::CaptureRenderFallbackMetadata(NodeObj, OutNode);
	CaptureBoundsDeltaMetadata(NodeObj, OutNode.Metadata);

	OutNode.Visual.Stroke = ParseStroke(NodeObj);
	CaptureFigmaStrokeMetadata(NodeObj, OutNode);
	if (OutNode.Type == EDesignNodeType::Path
		|| OutNode.Type == EDesignNodeType::Line
		|| OutNode.Type == EDesignNodeType::Polygon
		|| OutNode.Type == EDesignNodeType::Ellipse)
	{
		ParsePathGeometry(NodeObj, OutNode);
	}

	OutNode.Visual.CornerRadius = ParseCornerRadius(NodeObj);
	ParseExplicitGeometryMask(NodeObj, OutNode);
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
	const FNodeBounds* NodeCoordinateBounds,
	TArray<FDesignSkippedNodeReport>* SkippedDesignerNodes,
	FDesignParserBudget& ParseBudget,
	int32 Depth,
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
			Depth + 1))
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
	int32 Depth)
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

	const bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;

	// Position
	ParsePosition(NodeObj, ParentObj, ParentCoordinateBounds, *Node, bParentAutoLayout);

	// Size
	ParseSize(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);
	ParsePrototypeNodeData(NodeObj, Node->MutableSource().Prototype);
	ParseNodeVisualData(NodeObj, ParentObj, *Node);
	FNodeBounds NodeCoordinateBounds;
	const bool bHasNodeCoordinateBounds = bParentAutoLayout
		? TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), NodeCoordinateBounds)
		: TryGetVisualBounds(NodeObj, ParentObj, NodeCoordinateBounds);
	ParseNodeChildren(
		NodeObj,
		FigmaType,
		bParentAutoLayout,
		bHasNodeCoordinateBounds ? &NodeCoordinateBounds : nullptr,
		SkippedDesignerNodes,
		ParseBudget,
		Depth,
		*Node);

	FigmaParserMetadata::ApplyFigmaSemanticHints(NodeObj, *Node);
	FinalizeNodePaddingRuntimeWrap(*Node, bInsideInstanceSubtree);

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
		FigmaParserMetadata::CaptureSharedMetadataExtensions(SourceObj, OutDocument.Metadata);
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

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	return FigmaParserInternal::ParseDocument(FigmaJson, OutDocument);
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
