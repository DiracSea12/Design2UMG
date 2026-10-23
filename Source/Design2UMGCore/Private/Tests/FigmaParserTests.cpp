// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignReferenceExpansion.h"
#include "DesignTreeLimits.h"
#include "DesignSchema.h"
#include "UMGWidgetTypes.h"
#include "Design2UMGConverter.h"
#include "SizeEstimator.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "FigmaTestDocBuilder.h"
#include "VisualMapper.h"
#include "VisualMapperFillTransfer.h"
#include "PathWidgetBuilder.h"
#include "Design2UMGConverterVisualSupport.h"

using namespace Design2UMGCoreTests;

namespace
{
	static const TCHAR* FigmaPluginName = TEXT("Design2UMG");
	static const FString FigmaFixtureBase = TEXT("Source/Design2UMGCore/Tests/Fixtures/figma");

	bool LoadFigmaFixture(FAutomationTestBase& Test, const FString& FileName, FDesignDocument& OutDoc)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(FigmaPluginName);
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), FigmaFixtureBase, FileName);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *Path));
			return false;
		}
		return FFigmaParser::ParseDocument(Json, OutDoc);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserInputBudgetTest,
	"Design2UMG.Unit.FigmaParser.InputBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserInputBudgetTest::RunTest(const FString&)
{
	FDesignParserBudget Budget;
	for (int32 Index = 0; Index < DesignTreeLimits::MaxParsedNodeCount; ++Index)
	{
		if (!Budget.TryEnterNode(0))
		{
			AddError(FString::Printf(TEXT("Node budget rejected valid node %d"), Index));
			return false;
		}
	}
	TestFalse(TEXT("Node budget rejects one node beyond the shared limit"), Budget.TryEnterNode(0));

	FDesignParserBudget DepthBudget;
	TestTrue(TEXT("Depth at the shared limit is accepted"), DepthBudget.TryEnterNode(DesignTreeLimits::MaxNodeDepth));
	TestFalse(TEXT("Depth beyond the shared limit is rejected"), DepthBudget.TryEnterNode(DesignTreeLimits::MaxNodeDepth + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserRejectsDeepNodeTreeTest,
	"Design2UMG.Unit.FigmaParser.RejectsDeepNodeTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserRejectsDeepNodeTreeTest::RunTest(const FString&)
{
	FString Json = TEXT("{\"document\":");
	for (int32 Depth = 0; Depth <= DesignTreeLimits::MaxNodeDepth + 1; ++Depth)
	{
		Json += FString::Printf(
			TEXT("{\"type\":\"FRAME\",\"id\":\"%d\",\"name\":\"Node%d\",\"children\":["),
			Depth,
			Depth);
	}
	Json += TEXT("{\"type\":\"RECTANGLE\",\"id\":\"leaf\",\"name\":\"Leaf\"}");
	for (int32 Depth = 0; Depth <= DesignTreeLimits::MaxNodeDepth + 1; ++Depth)
	{
		Json += TEXT("]}");
	}
	Json += TEXT("}");

	AddExpectedError(TEXT("node tree exceeds the supported limits"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesignDocument Document;
	TestFalse(TEXT("Figma parser rejects a tree beyond the shared depth limit"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("Rejected parse leaves no partial tree"), Document.Children.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Basic node parsing: FRAME / TEXT / RECTANGLE
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserBasicNodesTest,
	"Design2UMG.Unit.FigmaParser.BasicNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserBasicNodesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("basic_nodes.json"), Doc))
		return false;

	TestEqual(TEXT("One root child"), Doc.Children.Num(), 1);
	const TSharedPtr<FDesignNode>& Frame = Doc.Children[0];
	TestTrue(TEXT("Frame valid"), Frame.IsValid());
	TestEqual(TEXT("Frame type"), Frame->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Frame has 2 children"), Frame->Children.Num(), 2);

	const TSharedPtr<FDesignNode>& TextNode = Frame->Children[0];
	TestTrue(TEXT("Text valid"), TextNode.IsValid());
	TestEqual(TEXT("Text type"), TextNode->Type, EDesignNodeType::Text);

	const TSharedPtr<FDesignNode>& RectNode = Frame->Children[1];
	TestTrue(TEXT("Rect valid"), RectNode.IsValid());
	TestEqual(TEXT("Rect type"), RectNode->Type, EDesignNodeType::Rectangle);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserHugSizingOutsideAutoLayoutParentTest,
	"Design2UMG.Unit.FigmaParser.HugSizingOutsideAutoLayoutParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserHugSizingOutsideAutoLayoutParentTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("parent"), TEXT("Parent"), TEXT("FRAME"), 0, 0, 800, 1000, FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeContainerNode(TEXT("hug"), TEXT("Hug"), TEXT("FRAME"), 0, 0, 400, 900,
			MakeLeafNode(TEXT("hug-child"), TEXT("Hug Child"), TEXT("RECTANGLE"), 12, 12, 100, 32,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(1, 1, 1))),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutSizingHorizontal\":\"HUG\",\"layoutSizingVertical\":\"HUG\","
				 "\"paddingLeft\":12,\"paddingRight\":12,\"paddingTop\":12,\"paddingBottom\":12")),
		*MakeContainerNode(TEXT("fill"), TEXT("Fill"), TEXT("FRAME"), 0, 0, 200, 80, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutSizingHorizontal\":\"FILL\",\"layoutSizingVertical\":\"FILL\"")),
		*MakeContainerNode(TEXT("fixed"), TEXT("Fixed"), TEXT("FRAME"), 0, 0, 160, 64, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\"")))));

	FDesignDocument Doc;
	TestTrue(TEXT("Document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One root parent"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid() || Doc.Children[0]->Children.Num() != 3)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& HugNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Self auto-layout HUG width becomes fit-content"), HugNode->Transform.Width.IsFit());
	TestTrue(TEXT("Self auto-layout HUG height becomes fit-content"), HugNode->Transform.Height.IsFit());
	TestEqual(TEXT("Stale HUG width is not retained as a minimum"), HugNode->Transform.Width.Value, 0.0f);
	TestEqual(TEXT("Stale HUG height is not retained as a minimum"), HugNode->Transform.Height.Value, 0.0f);

	const FVector2D ResolvedHugSize = SizeEstimator::PromoteFitContentSizes(HugNode, Doc,
		[&Doc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, Doc).ExpandedNode; });
	TestEqual(TEXT("HUG width resolves from child and padding"), ResolvedHugSize.X, 124.0);
	TestEqual(TEXT("HUG height resolves from child and padding"), ResolvedHugSize.Y, 56.0);

	const TSharedPtr<FDesignNode>& FillNode = Doc.Children[0]->Children[1];
	TestTrue(TEXT("FILL without an auto-layout parent keeps fixed width"), FillNode->Transform.Width.IsFixed());
	TestTrue(TEXT("FILL without an auto-layout parent keeps fixed height"), FillNode->Transform.Height.IsFixed());
	TestEqual(TEXT("Fixed FILL width keeps its source bounds"), FillNode->Transform.Width.Value, 200.0f);
	TestEqual(TEXT("Fixed FILL height keeps its source bounds"), FillNode->Transform.Height.Value, 80.0f);

	const TSharedPtr<FDesignNode>& FixedNode = Doc.Children[0]->Children[2];
	TestTrue(TEXT("Missing horizontal sizing stays fixed"), FixedNode->Transform.Width.IsFixed());
	TestTrue(TEXT("Missing vertical sizing stays fixed"), FixedNode->Transform.Height.IsFixed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserStrokeDashMetadataTest,
	"Design2UMG.Unit.FigmaParser.StrokeDashMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserStrokeDashMetadataTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeLeafNode(TEXT("dashed-vector"), TEXT("Dashed Vector"), TEXT("VECTOR"), 0, 0, 0.0001, 306,
		FString::Printf(
			TEXT(",\"absoluteRenderBounds\":%s,"
				 "\"strokes\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.9,\"b\":1,\"a\":1}}],"
				 "\"strokeWeight\":5.11,\"strokeAlign\":\"CENTER\",\"strokeCap\":\"ROUND\",\"strokeJoin\":\"BEVEL\","
				 "\"strokeDashes\":[21.17,21.17]"),
			*MakeBoundsJson(-3, -3, 6, 312))));

	FDesignDocument Document;
	TestTrue(TEXT("Dashed VECTOR parses"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("One dashed VECTOR is emitted"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestEqual(TEXT("Stroke cap is normalized for shared conversion"), Node->Visual.Stroke.Cap, EDesignStrokeCap::Circular);
	TestEqual(TEXT("Stroke join is normalized for shared conversion"), Node->Visual.Stroke.Join, EDesignStrokeJoin::Bevel);
	TestEqual(TEXT("Stroke dash count is normalized for shared conversion"), Node->Visual.Stroke.DashPattern.Num(), 2);
	if (Node->Visual.Stroke.DashPattern.Num() == 2)
	{
		TestEqual(TEXT("First shared stroke dash"), Node->Visual.Stroke.DashPattern[0], 21.17f);
		TestEqual(TEXT("Second shared stroke dash"), Node->Visual.Stroke.DashPattern[1], 21.17f);
	}
	TestEqual(TEXT("Dashed stroke cap metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.cap")), TEXT("ROUND"));
	TestEqual(TEXT("Stroke join metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.join")), TEXT("BEVEL"));
	TestEqual(TEXT("Stroke dash metadata is preserved"), Node->Metadata.FindRef(TEXT("stroke.dashes")), TEXT("21.17,21.17"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserExpandedDropShadowUsesLayoutBoundsTest,
	"Design2UMG.Unit.FigmaParser.ExpandedDropShadowUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserExpandedDropShadowUsesLayoutBoundsTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("card"), TEXT("Card"), TEXT("GROUP"), 0, 0, 400, 200,
		MakeLeafNode(TEXT("background"), TEXT("Background"), TEXT("RECTANGLE"), 40, 32, 210, 86,
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,"
					 "\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"offset\":{\"x\":0,\"y\":0},\"radius\":40,\"showShadowBehindNode\":true}],"
					 "\"fillGeometry\":[{\"path\":\"M0 0 L210 0 L210 86 Z\",\"windingRule\":\"NONZERO\"},{\"path\":\"M10 10 L20 10 L20 20 Z\",\"windingRule\":\"EVENODD\"}],"
						"\"strokeGeometry\":[{\"path\":\"M1 1 L209 1 L209 85 Z\",\"windingRule\":\"NONZERO\"}],"
						"\"fills\":[%s]"),
				*MakeBoundsJson(0, -8, 290, 166), *MakeSolidFillJson(1, 1, 1)))));

	FDesignDocument Document;
	TestTrue(TEXT("Expanded drop-shadow node parses"), FFigmaParser::ParseDocument(Json, Document));
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid()
		|| Document.Children[0]->Children.Num() != 1 || !Document.Children[0]->Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Background = Document.Children[0]->Children[0];
	TestEqual(TEXT("Drop-shadow node keeps layout width"), Background->Transform.Width.Value, 210.0f);
	TestEqual(TEXT("Drop-shadow node keeps layout height"), Background->Transform.Height.Value, 86.0f);
	const auto& Geometry = Background->Shape.PaintGeometry;
	if (!TestTrue(TEXT("Fill and stroke geometry are independently retained"), Geometry.FillPaths.IsSet() && Geometry.StrokePaths.IsSet()))
		return false;
	if (!TestEqual(TEXT("All fill contours are retained"), Geometry.FillPaths.GetValue().Num(), 2))
		return false;
	TestEqual(TEXT("Stroke contour is retained"), Geometry.StrokePaths.GetValue().Num(), 1);
	TestEqual(TEXT("Each contour keeps its own fill rule"), Geometry.FillPaths.GetValue()[1].FillRule, FString(TEXT("evenodd")));
	FUMGWidgetNode Widget;
	VisualMapper::ApplyVisualProperties(Widget, Background);
	if (!TestEqual(TEXT("One normalized shadow"), Widget.Shadow.BoxShadows.Num(), 1))
		return false;
	TestTrue(TEXT("Shadow behind flag reaches the shared shadow layer"), Widget.Shadow.BoxShadows[0].bShowShadowBehindNode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserPathLocalCanvasTest,
	"Design2UMG.Unit.FigmaParser.PathLocalCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserPathLocalCanvasTest::RunTest(const FString&)
{
	const FString Path = TEXT("M0 0 L86.25 0 L86.25 210.5 Z");
	const FString Vector = MakeLeafNode(TEXT("path"), TEXT("Path"), TEXT("VECTOR"), 0, 0, 210.5, 86.25,
		FString::Printf(TEXT(",\"size\":{\"x\":86.25,\"y\":210.5},\"rotation\":90,"
			"\"fillGeometry\":[{\"path\":\"%s\",\"windingRule\":\"NONZERO\"}],\"fills\":[%s]"),
			*Path, *MakeSolidFillJson(1, 1, 1)));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Source-local path parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Vector), Document))
		|| !TestEqual(TEXT("One path is retained"), Document.Children.Num(), 1)) return false;
	const FUMGWidgetNode Widget = PathWidgetBuilder::CreatePathWidget(Document.Children[0]);
	TestTrue(TEXT("Local coordinates do not use the rotated absolute bounding box"),
		Widget.Path.ViewBox.IsSet() && Widget.Path.ViewBox->Equals(FVector4(0, 0, 86.25, 210.5)));
	TestTrue(TEXT("Target canvas and source path retain their separate contracts"),
		Widget.Path.CanvasSize.IsSet() && Widget.Path.CanvasSize->Equals(FVector2D(210.5, 86.25))
		&& Widget.Path.Geometry.Get(FString()) == Path);
	return true;
}

// ---------------------------------------------------------------------------
// Fill types: solid / gradient / image
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserFillTypesTest,
	"Design2UMG.Unit.FigmaParser.FillTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserFillTypesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// First root child is FillsFrame with 3 children
	TestTrue(TEXT("Has root children"), Doc.Children.Num() >= 1);
	const TSharedPtr<FDesignNode>& FillsFrame = Doc.Children[0];
	TestTrue(TEXT("FillsFrame valid"), FillsFrame.IsValid());
	TestEqual(TEXT("FillsFrame has 3 children"), FillsFrame->Children.Num(), 3);

	// Solid fill child
	const TSharedPtr<FDesignNode>& SolidNode = FillsFrame->Children[0];
	TestTrue(TEXT("SolidNode valid"), SolidNode.IsValid());
	TestEqual(TEXT("SolidNode fill type"), SolidNode->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("SolidNode fills count"), SolidNode->Visual.Fills.Num(), 1);
	TestFalse(TEXT("SolidNode solid color present"), SolidNode->Visual.TopmostFill().SolidColor.IsEmpty());

	// Gradient fill child
	const TSharedPtr<FDesignNode>& GradNode = FillsFrame->Children[1];
	TestTrue(TEXT("GradNode valid"), GradNode.IsValid());
	TestEqual(TEXT("GradNode fill type"), GradNode->Visual.TopmostFill().Type, EDesignFillType::Gradient);
	TestEqual(TEXT("GradNode gradient type"), GradNode->Visual.TopmostFill().Gradient.GradientType, EDesignGradientType::Linear);
	TestEqual(TEXT("GradNode gradient stops"), GradNode->Visual.TopmostFill().Gradient.Colors.Num(), 2);
	TestEqual(TEXT("GradNode first stop position"), GradNode->Visual.TopmostFill().Gradient.Colors[0].Position, 0.0f);
	TestEqual(TEXT("GradNode second stop position"), GradNode->Visual.TopmostFill().Gradient.Colors[1].Position, 1.0f);

	// Image fill child
	const TSharedPtr<FDesignNode>& ImgNode = FillsFrame->Children[2];
	TestTrue(TEXT("ImgNode valid"), ImgNode.IsValid());
	TestEqual(TEXT("ImgNode fill type"), ImgNode->Visual.TopmostFill().Type, EDesignFillType::Image);
	TestEqual(TEXT("ImgNode image ref"), ImgNode->Visual.TopmostFill().Image.Url, FString(TEXT("abc123")));
	TestEqual(TEXT("ImgNode image mode"), ImgNode->Visual.TopmostFill().Image.Mode, EDesignImageMode::Fill);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserGradientGeometryTest,
	"Design2UMG.Gradient.FigmaParserGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserGradientGeometryTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("2:1"), TEXT("RadialGradientNode"), TEXT("RECTANGLE"), 0, 0, 120, 80,
			TEXT(",\"fills\":[{"
				 "\"type\":\"GRADIENT_RADIAL\","
				 "\"gradientHandlePositions\":["
				 "{\"x\":0.25,\"y\":0.75},{\"x\":0.65,\"y\":0.75},{\"x\":0.25,\"y\":1.15}],"
				 "\"gradientStops\":["
				 "{\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1},\"position\":0},"
				 "{\"color\":{\"r\":0,\"g\":1,\"b\":0,\"a\":1},\"position\":0.4},"
				 "{\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1},\"position\":1}]}]")),
		TEXT(",\"components\":{},\"componentSets\":{},\"styles\":{}"));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One parsed root child"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestEqual(TEXT("Parsed gradient fill type"), Node->Visual.TopmostFill().Type, EDesignFillType::Gradient);
	TestEqual(TEXT("Parsed gradient type"), Node->Visual.TopmostFill().Gradient.GradientType, EDesignGradientType::Radial);
	TestEqual(TEXT("Parsed gradient stops"), Node->Visual.TopmostFill().Gradient.Colors.Num(), 3);
	TestTrue(TEXT("Parsed radial center present"), Node->Visual.TopmostFill().Gradient.Center.IsSet());
	TestTrue(TEXT("Parsed radial size present"), Node->Visual.TopmostFill().Gradient.Size.IsSet());
	const FVector2D ParsedCenter = Node->Visual.TopmostFill().Gradient.Center.Get(FVector2D::ZeroVector);
	const FVector2D ParsedSize = Node->Visual.TopmostFill().Gradient.Size.Get(FVector2D::ZeroVector);
	TestTrue(TEXT("Parsed radial center X"),
		FMath::IsNearlyEqual(ParsedCenter.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Parsed radial center Y"),
		FMath::IsNearlyEqual(ParsedCenter.Y, 0.75f, 0.0001f));
	TestTrue(TEXT("Parsed radial size width"),
		FMath::IsNearlyEqual(ParsedSize.X, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed radial size height"),
		FMath::IsNearlyEqual(ParsedSize.Y, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed middle stop position"),
		FMath::IsNearlyEqual(Node->Visual.TopmostFill().Gradient.Colors[1].Position, 0.4f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserVisualExplicitClearTest,
	"Design2UMG.Unit.FigmaParser.VisualExplicitClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserVisualExplicitClearTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("explicit-clear"), TEXT(""), TEXT("RECTANGLE"), 0, 0, 80, 40,
			TEXT(",\"fills\":[],\"effects\":[],\"fillGeometry\":[]")));

	FDesignDocument Document;
	TestTrue(TEXT("Figma explicit-clear fixture parses"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("One explicit-clear node is emitted"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const FDesignVisualNodeData& Visual = Document.Children[0]->Visual;
	TestTrue(TEXT("Empty fills are represented as an explicit shared clear"), Visual.bFillExplicitClear);
	TestTrue(TEXT("Empty effects are represented as an explicit shared clear"), Visual.bEffectsExplicitClear);
	TestEqual(TEXT("Explicit fill clear has no fill layers"), Visual.Fills.Num(), 0);
	TestEqual(TEXT("Explicit effect clear has no effects"), Visual.Effects.Num(), 0);
	const auto& Geometry = Document.Children[0]->Shape.PaintGeometry;
	TestTrue(TEXT("Known empty fill geometry stays distinct from missing geometry"), Geometry.FillPaths.IsSet() && Geometry.FillPaths.GetValue().IsEmpty());
	TestFalse(TEXT("Missing stroke geometry stays missing"), Geometry.StrokePaths.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExpandedStrokePaintTest,
	"Design2UMG.Unit.FigmaParser.ExpandedStrokePaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaExpandedStrokePaintTest::RunTest(const FString&)
{
	const FString Upper = TEXT("M-2.5 -2.5 L29 -2.5 L29 2.5 L-2.5 2.5 Z");
	const FString Lower = TEXT("M-2.5 14.75 L29 14.75 L29 19.75 L-2.5 19.75 Z");
	const FString Source = MakeLeafNode(TEXT("outline"), TEXT("Outline"), TEXT("VECTOR"), 0, 0, 26.5, 17.25,
		FString::Printf(TEXT(",\"size\":{\"x\":26.5,\"y\":17.25},"
			"\"absoluteRenderBounds\":{\"x\":-2.5,\"y\":-2.5,\"width\":31.5,\"height\":22.25},"
			"\"fillGeometry\":[],\"fills\":[],\"strokeWeight\":5,\"strokeAlign\":\"CENTER\",\"strokes\":[%s],"
			"\"strokeGeometry\":[{\"path\":\"%s\",\"windingRule\":\"NONZERO\"},"
			"{\"path\":\"%s\",\"windingRule\":\"EVENODD\"}]"), *MakeSolidFillJson(0, 0, 1), *Upper, *Lower));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Expanded stroke source parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Source), Document))) return false;
	if (!TestEqual(TEXT("One source path"), Document.Children.Num(), 1)) return false;
	const auto& Node = Document.Children[0];
	FUMGWidgetNode Surface = PathWidgetBuilder::CreatePathWidget(Node);
	TestFalse(TEXT("Expanded outline is not restroked as a centerline"), Surface.Path.StrokeThickness.IsSet());
	FUMGWidgetNode Painted = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Surface), Node);
	if (!TestTrue(TEXT("Static source outlines compose into one native surface"), Painted.Children.IsEmpty() && Painted.Path.SourcePaintGeometry.IsSet())) return false;
	const auto& Common = Painted.Path.SourcePaintGeometry.GetValue();
	if (!TestTrue(TEXT("Common geometry contains both source outlines"), Common.Geometry.StrokePaths.IsSet() && Common.Geometry.StrokePaths->Num() == 2)) return false;
	TestEqual(TEXT("First source contour unchanged"), Common.Geometry.StrokePaths.GetValue()[0].Path, Upper);
	TestEqual(TEXT("Second source contour unchanged"), Common.Geometry.StrokePaths.GetValue()[1].Path, Lower);
	TestEqual(TEXT("Common geometry retains each path rule"), Common.Geometry.StrokePaths.GetValue()[1].FillRule, FString(TEXT("evenodd")));
	TestEqual(TEXT("Open center strokes do not acquire alignment clipping"), Common.StrokeAlignment, FString(TEXT("center")));
	TestTrue(TEXT("Source local canvas retained"), Painted.Path.ViewBox.IsSet() && Painted.Path.ViewBox->Equals(FVector4(0, 0, 26.5, 17.25)));
	TestFalse(TEXT("Filled source outline has no second stroke"), Painted.Path.StrokeThickness.IsSet());
	TestEqual(TEXT("Source stroke metadata remains unchanged"), Node->Visual.Stroke.Thickness.Uniform, 5.0f);

	auto InsideNode = MakeShared<FDesignNode>(*Node);
	InsideNode->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	InsideNode->Shape.PaintGeometry.FillPaths = TArray<FDesignGeometryPath>{{TEXT("M0 0L26.5 0L26.5 17.25L0 17.25Z"), TEXT("evenodd")}};
	const FUMGWidgetNode Inside = VisualMapperFillTransfer::WrapWithPaintedStroke(PathWidgetBuilder::CreatePathWidget(InsideNode), InsideNode);
	if (!TestTrue(TEXT("Inside alignment stays in the source context"), Inside.Path.SourcePaintGeometry.IsSet()
		&& Inside.Path.SourcePaintGeometry->StrokeAlignment == TEXT("inner"))) return false;
	TestEqual(TEXT("The interior retains its own winding rule"), Inside.Path.SourcePaintGeometry->Geometry.FillPaths.GetValue()[0].FillRule, FString(TEXT("evenodd")));

	auto PrimitiveNode = MakeShared<FDesignNode>(*Node);
	PrimitiveNode->Type = EDesignNodeType::Rectangle;
	FUMGWidgetNode NativeBorder;
	NativeBorder.Type = TEXT("Border");
	VisualMapper::ApplyStroke(NativeBorder, PrimitiveNode->Visual.Stroke);
	FUMGWidgetNode NativePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(NativeBorder), PrimitiveNode);
	TestEqual(TEXT("An existing native stroke keeps its paint surface"), NativePaint.Type, FString(TEXT("Border")));
	TestEqual(TEXT("Native stroke keeps the declared thickness"), NativePaint.Brush.OutlineThickness.Get(0.0f), 5.0f);
	TestFalse(TEXT("Native stroke is not replaced by source area geometry"), NativePaint.Path.Geometry.IsSet());
	TestTrue(TEXT("A native primitive adds no source stroke layers"), NativePaint.Children.IsEmpty());

	auto ResourceNode = MakeShared<FDesignNode>(*PrimitiveNode);
	FDesignFill ResourceFill;
	ResourceFill.Type = EDesignFillType::Image;
	ResourceFill.Image.Url = TEXT("/Textures/paint.png");
	ResourceNode->Visual.Fills.Add(ResourceFill);
	FUMGWidgetNode ResourceBorder;
	ResourceBorder.Type = TEXT("Border");
	ResourceBorder.Brush.Image.TexturePath = ResourceFill.Image.Url;
	VisualMapper::ApplyStroke(ResourceBorder, ResourceNode->Visual.Stroke);
	TestTrue(TEXT("Resource fill uses Box rather than native outline drawing"),
		ResourceBorder.Brush.DrawAs.Get(ESlateBrushDrawType::NoDrawType) == ESlateBrushDrawType::Box);
	FUMGWidgetNode ResourcePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ResourceBorder), ResourceNode);
	if (!TestTrue(TEXT("Resource border keeps a single background surface"), ResourcePaint.Type == UMGWidgetTypes::Border
		&& ResourcePaint.Children.IsEmpty() && ResourcePaint.Path.SourcePaintGeometry.IsSet())) return false;
	TestEqual(TEXT("Resource fill is retained in the complete static paint list"),
		ResourcePaint.Path.SourcePaintGeometry->FillPaints.GetValue()[0].Image.TexturePath.Get(FString()), ResourceFill.Image.Url);
	TestEqual(TEXT("Box brush still paints the source outline geometry"),
		ResourcePaint.Path.SourcePaintGeometry->Geometry.StrokePaths.GetValue()[0].Path, Upper);

	const FString FillPath = TEXT("M0 0 L26.5 0 L26.5 17.25 L0 17.25 Z");
	auto FilledNode = MakeShared<FDesignNode>(*Node);
	FDesignFill Fill;
	Fill.Type = EDesignFillType::Color;
	Fill.SolidColor = TEXT("#FF0000");
	FilledNode->Visual.Fills.Add(Fill);
	FilledNode->Shape.Geometry = FillPath;
	FilledNode->Shape.FillRule = TEXT("nonzero");
	FilledNode->Shape.PaintGeometry.FillPaths = TArray<FDesignGeometryPath>{{FillPath, TEXT("nonzero")}};
	auto TranslucentNode = MakeShared<FDesignNode>(*FilledNode);
	TranslucentNode->Transform.SetOpacityValue(0.35f);
	FUMGWidgetNode TranslucentSurface = PathWidgetBuilder::CreatePathWidget(TranslucentNode);
	TranslucentSurface.Render.RenderOpacity = 0.35f;
	const FUMGWidgetNode Combined = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(TranslucentSurface), TranslucentNode);
	TestTrue(TEXT("A leaf source fill and stroke share one paint surface"), Combined.Type == TEXT("Image") && Combined.Children.IsEmpty());
	TestTrue(TEXT("Object opacity remains on the final single image"), Combined.Render.RenderOpacity.IsSet()
		&& FMath::IsNearlyEqual(Combined.Render.RenderOpacity.GetValue(), 0.35f));
	TestTrue(TEXT("Combined source regions retain their independent rules"), Combined.Path.SourcePaintGeometry.IsSet()
		&& Combined.Path.SourcePaintGeometry->Geometry.StrokePaths.GetValue()[1].FillRule == TEXT("evenodd"));
	TestTrue(TEXT("Combined paint carries a stroke color without reintroducing a centerline"), Combined.Path.SourcePaintGeometry->StrokePaint.IsSet()
		&& !Combined.Path.StrokeThickness.IsSet());
	auto GradientNode = MakeShared<FDesignNode>(*FilledNode);
	GradientNode->Visual.Fills.Reset();
	FDesignFill GradientFill;
	GradientFill.Type = EDesignFillType::Gradient;
	FDesignGradientStop& FirstStop = GradientFill.Gradient.Colors.AddDefaulted_GetRef();
	FirstStop.Color = TEXT("#80402080");
	FirstStop.Position = 0.0f;
	FDesignGradientStop& LastStop = GradientFill.Gradient.Colors.AddDefaulted_GetRef();
	LastStop.Color = TEXT("#20408040");
	LastStop.Position = 1.0f;
	GradientNode->Visual.Fills.Add(GradientFill);
	FUMGWidgetNode GradientSurface = PathWidgetBuilder::CreatePathWidget(GradientNode);
	VisualMapper::ApplyVisualProperties(GradientSurface, GradientNode);
	TestTrue(TEXT("The real single-gradient builder supplies a non-white placeholder"),
		GradientSurface.Path.FillColor.IsSet() && !GradientSurface.Path.FillColor->Equals(FLinearColor::White));
	const FLinearColor Tint(0.5f, 0.75f, 1.0f, 0.6f);
	GradientSurface.Brush.BrushColor = Tint;
	const auto OriginalStops = GradientSurface.Brush.Gradient.Stops;
	GradientSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(GradientSurface), GradientNode);
	if (!TestTrue(TEXT("A real single gradient reaches the complete source surface"), GradientSurface.Path.SourcePaintGeometry.IsSet()
		&& GradientSurface.Path.SourcePaintGeometry->FillPaints.IsSet()
		&& GradientSurface.Path.SourcePaintGeometry->FillPaints->Num() == 1)) return false;
	const auto& GradientPaint = GradientSurface.Path.SourcePaintGeometry->FillPaints.GetValue()[0];
	TestTrue(TEXT("Gradient placeholder is not applied as a second tint; real tint alpha remains"), GradientPaint.Color.Equals(Tint));
	TestTrue(TEXT("Gradient stop colors and alpha remain owned by the gradient"), GradientPaint.Gradient.Stops.Num() == 2
		&& GradientPaint.Gradient.Stops[0].Color.Equals(OriginalStops[0].Color)
		&& GradientPaint.Gradient.Stops[1].Color.Equals(OriginalStops[1].Color));

	FUMGWidgetNode FillSurface = PathWidgetBuilder::CreatePathWidget(FilledNode);
	FUMGWidgetNode Content;
	Content.Id = TEXT("Content");
	FillSurface.Children.Add(Content);
	FUMGWidgetNode Filled = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FillSurface), FilledNode);
	TestTrue(TEXT("A composed surface retains content without a second paint child"),
		Filled.Type == UMGWidgetTypes::Border && Filled.Children.Num() == 1 && Filled.Children[0].Id == Content.Id);
	TestTrue(TEXT("The complete source surface keeps fill and stroke paint descriptions"),
		Filled.Path.SourcePaintGeometry.IsSet() && Filled.Path.SourcePaintGeometry->FillPaints.IsSet()
		&& Filled.Path.SourcePaintGeometry->FillPaints->Num() == 1 && Filled.Path.SourcePaintGeometry->StrokePaint.IsSet());
	auto MultiNode = MakeShared<FDesignNode>(*TranslucentNode);
	FDesignFill SecondFill;
	SecondFill.Type = EDesignFillType::Image;
	SecondFill.Image.Url = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	SecondFill.Image.Opacity = 0.4f;
	MultiNode->Visual.Fills.Add(SecondFill);
	int32 PaintLayers = 0;
	FUMGWidgetNode MultiContent = PathWidgetBuilder::CreatePathWidget(MultiNode);
	MultiContent.Render.RenderOpacity = 0.35f;
	FUMGWidgetNode MultiSurface = VisualMapperFillTransfer::WrapWithMultiFill(
		MoveTemp(MultiContent), MultiNode, FVisualMapperOptions(), &PaintLayers);
	MultiSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(MultiSurface), MultiNode, PaintLayers);
	if (!TestTrue(TEXT("All static fills are owned by the final surface"), MultiSurface.Path.SourcePaintGeometry.IsSet()
		&& MultiSurface.Path.SourcePaintGeometry->FillPaints.IsSet())) return false;
	const auto& MultiPaints = MultiSurface.Path.SourcePaintGeometry->FillPaints.GetValue();
	if (!TestEqual(TEXT("Both static fills remain in their original order"), MultiPaints.Num(), 2)) return false;
	TestEqual(TEXT("Image paint opacity is applied once"), MultiPaints[1].Color.A, SecondFill.Image.Opacity, 0.0f);
	TestEqual(TEXT("Object opacity remains on the final surface"), MultiSurface.Render.RenderOpacity.Get(1.0f), 0.35f, 0.0f);
	TestFalse(TEXT("A retained layout carrier has no independent fill or stroke paint"),
		MultiSurface.Children.ContainsByPredicate([](const FUMGWidgetNode& Child)
		{
			const FUMGVisualBrushData& Brush = Child.Brush;
			return Child.Path.HasAny()
				|| Brush.OutlineCornerRadius.IsSet() || Brush.CornerRadii.IsSet()
				|| Brush.OutlineThickness.IsSet() || Brush.Margin.IsSet() || Brush.DrawAs.IsSet()
				|| Brush.BrushColor.IsSet() || Brush.OutlineColor.IsSet()
				|| Brush.MaterialRef.IsSet() || Brush.MaterialParams.IsSet() || Brush.Shape.IsSet()
				|| Brush.Image.HasAny() || Brush.Gradient.HasAny() || Brush.Shader.IsSet()
				|| !Child.Children.IsEmpty();
		}));

	const FString FitSource = MakeLeafNode(TEXT("fit-surface"), TEXT("Fit surface"), TEXT("FRAME"), 0, 0, 100, 80,
		TEXT(",\"size\":{\"x\":100,\"y\":80},\"opacity\":0.35,"
			"\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1}},"
			"{\"type\":\"IMAGE\",\"scaleMode\":\"FIT\",\"opacity\":0.4,\"imageRef\":\"/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture\"}],"
			"\"strokes\":[{\"type\":\"GRADIENT_LINEAR\",\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":0,\"y\":1}],"
			"\"gradientStops\":[{\"position\":0,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}]}],"
			"\"individualStrokeWeights\":{\"top\":1,\"right\":2,\"bottom\":1,\"left\":2},\"strokeAlign\":\"INSIDE\","
			"\"fillGeometry\":[{\"path\":\"M0 0L100 0L100 80L0 80Z\",\"windingRule\":\"NONZERO\"}],"
			"\"strokeGeometry\":[{\"path\":\"M-2 -2L102 -2L102 82L-2 82Z M2 2L98 2L98 78L2 78Z\",\"windingRule\":\"EVENODD\"}]"));
	FDesignDocument FitDocument;
	if (!TestTrue(TEXT("A supported FIT frame with nonuniform gradient stroke parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(FitSource), FitDocument))
		|| !TestEqual(TEXT("The FIT source has one node"), FitDocument.Children.Num(), 1)) return false;
	const auto& FitNode = FitDocument.Children[0];
	TestTrue(TEXT("The frame takes the existing ScaleBox fill route"), FitNode->Shape.Geometry.IsEmpty() && !FitNode->Visual.Stroke.Thickness.bUniform);
	FUMGWidgetNode FitContent;
	FitContent.Type = TEXT("CanvasPanel");
	FitContent.Id = FitNode->Id;
	FitContent.Name = FitNode->Name;
	VisualMapper::ApplyVisualProperties(FitContent, FitNode);
	int32 FitLayers = 0;
	FUMGWidgetNode FitSurface = VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(FitContent), FitNode, FVisualMapperOptions(), &FitLayers);
	if (!TestTrue(TEXT("The real mapper creates the supported FIT ScaleBox image layer"), FitLayers == 2
		&& FitSurface.Children[1].Type == UMGWidgetTypes::ScaleBox && FitSurface.Children[1].Children.Num() == 1)) return false;
	TestTrue(TEXT("The generated ScaleBox child retains Fill/Fill slots"),
		FitSurface.Children[1].Children[0].Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& FitSurface.Children[1].Children[0].Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	const FString FitImage = FitSurface.Children[1].Children[0].Brush.Image.TexturePath.Get(FString());
	FitSurface = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FitSurface), FitNode, FitLayers);
	if (!TestTrue(TEXT("The FIT frame reaches complete surface paint"), FitSurface.Path.SourcePaintGeometry.IsSet()
		&& FitSurface.Path.SourcePaintGeometry->FillPaints.IsSet() && FitSurface.Path.SourcePaintGeometry->FillPaints->Num() == 2)) return false;
	const auto& FitPaint = FitSurface.Path.SourcePaintGeometry->FillPaints.GetValue()[1];
	TestEqual(TEXT("The captured FIT paint retains the actual nested image"), FitPaint.Image.TexturePath.Get(FString()), FitImage);
	TestEqual(TEXT("Generated Fill slots are preserved by existing static Stretch sampling"), FitPaint.Image.CropFitMode.Get(FString()), FString(TEXT("Stretch")));
	TestTrue(TEXT("Nested image paint alpha and object opacity remain separate"), FMath::IsNearlyEqual(FitPaint.Color.A, 0.4f)
		&& FMath::IsNearlyEqual(FitSurface.Render.RenderOpacity.Get(1.0f), 0.35f));

	auto SideNode = MakeShared<FDesignNode>(*FilledNode);
	SideNode->Type = EDesignNodeType::Frame;
	SideNode->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	SideNode->Visual.Stroke.Thickness.bUniform = false;
	SideNode->Visual.Stroke.Thickness.Top = SideNode->Visual.Stroke.Thickness.Right = SideNode->Visual.Stroke.Thickness.Bottom = 0.0f;
	SideNode->Visual.Stroke.Thickness.Left = 2.0f;
	FUMGWidgetNode SideContent;
	SideContent.Type = TEXT("HorizontalBox");
	VisualMapper::ApplyVisualProperties(SideContent, SideNode);
	bool bStrokePainted = false;
	FUMGWidgetNode SidePaint = VisualMapper::WrapWithBorder(MoveTemp(SideContent), SideNode, &bStrokePainted);
	TestTrue(TEXT("A successfully constructed explicit side owns the stroke"), bStrokePainted);
	const int32 SideLayers = SidePaint.Children.Num();
	FUMGWidgetNode SideEffects = Design2UMGConverterVisualSupport::ApplyVisualEffectWrappers(
		MoveTemp(SidePaint), SideNode, FVisualMapperOptions(), bStrokePainted);
	TestEqual(TEXT("Later paint does not duplicate an already painted side"), SideEffects.Children.Num(), SideLayers);
	auto UnpaintedSideNode = MakeShared<FDesignNode>(*SideNode);
	UnpaintedSideNode->Visual.Stroke.Fill->Type = EDesignFillType::Gradient;
	FUMGWidgetNode UnpaintedContent;
	UnpaintedContent.Type = TEXT("HorizontalBox");
	bool bUnpaintedStroke = true;
	VisualMapper::WrapWithBorder(MoveTemp(UnpaintedContent), UnpaintedSideNode, &bUnpaintedStroke);
	TestFalse(TEXT("A side stroke that did not resolve a solid color has no owner"), bUnpaintedStroke);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserLinearGradientEndpointsTest,
	"Design2UMG.Gradient.FigmaParserLinearEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserLinearGradientEndpointsTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("2:1"), TEXT("LinearGradientNode"), TEXT("RECTANGLE"), 0, 0, 120, 80,
			TEXT(",\"fills\":[{"
				 "\"type\":\"GRADIENT_LINEAR\","
				 "\"gradientHandlePositions\":["
				 "{\"x\":0.2,\"y\":0.3},{\"x\":0.8,\"y\":0.7},{\"x\":0.0,\"y\":1.0}],"
				 "\"gradientStops\":["
				 "{\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1},\"position\":0},"
				 "{\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1},\"position\":1}]}]")));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma linear gradient parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One parsed linear gradient node"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid())
	{
		return false;
	}

	const FDesignGradient& Gradient = Doc.Children[0]->Visual.TopmostFill().Gradient;
	TestEqual(TEXT("Parsed linear gradient type"), Gradient.GradientType, EDesignGradientType::Linear);
	TestTrue(TEXT("Parsed linear start point"), Gradient.StartPoint.IsSet());
	TestTrue(TEXT("Parsed linear end point"), Gradient.EndPoint.IsSet());
	if (!Gradient.StartPoint.IsSet() || !Gradient.EndPoint.IsSet())
	{
		return false;
	}

	TestTrue(TEXT("Linear start X preserved"), FMath::IsNearlyEqual(Gradient.StartPoint.GetValue().X, 0.2f, 0.0001f));
	TestTrue(TEXT("Linear start Y preserved"), FMath::IsNearlyEqual(Gradient.StartPoint.GetValue().Y, 0.3f, 0.0001f));
	TestTrue(TEXT("Linear end X preserved"), FMath::IsNearlyEqual(Gradient.EndPoint.GetValue().X, 0.8f, 0.0001f));
	TestTrue(TEXT("Linear end Y preserved"), FMath::IsNearlyEqual(Gradient.EndPoint.GetValue().Y, 0.7f, 0.0001f));
	return true;
}

// ---------------------------------------------------------------------------
// Effects: blur / shadow
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserEffectsTest,
	"Design2UMG.Unit.FigmaParser.Effects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserEffectsTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// Second root child is EffectsFrame
	TestTrue(TEXT("Has effects frame"), Doc.Children.Num() >= 2);
	const TSharedPtr<FDesignNode>& EffectsFrame = Doc.Children[1];
	TestTrue(TEXT("EffectsFrame valid"), EffectsFrame.IsValid());
	TestEqual(TEXT("Effects count"), EffectsFrame->Visual.Effects.Num(), 2);
	TestEqual(TEXT("First effect type"), EffectsFrame->Visual.Effects[0].Type, EDesignEffectType::Blur);
	TestEqual(TEXT("First effect radius"), EffectsFrame->Visual.Effects[0].Radius, 4.0f);
	TestEqual(TEXT("Second effect type"), EffectsFrame->Visual.Effects[1].Type, EDesignEffectType::Shadow);
	TestEqual(TEXT("Second effect offset x"), EffectsFrame->Visual.Effects[1].Shadow.OffsetX, 2.0f);
	TestEqual(TEXT("Second effect offset y"), EffectsFrame->Visual.Effects[1].Shadow.OffsetY, 4.0f);
	TestFalse(TEXT("Omitted shadow behind flag uses the source default"), EffectsFrame->Visual.Effects[1].Shadow.bShowShadowBehindNode);

	return true;
}

// ---------------------------------------------------------------------------
// Variable references: boundVariables field
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserBoundVariablesTest,
	"Design2UMG.Unit.FigmaParser.BoundVariables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserBoundVariablesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadFigmaFixture(*this, TEXT("fills_effects_vars.json"), Doc))
		return false;

	// Third root child is BoundVarsFrame
	TestTrue(TEXT("Has bound vars frame"), Doc.Children.Num() >= 3);
	const TSharedPtr<FDesignNode>& VarsFrame = Doc.Children[2];
	TestTrue(TEXT("VarsFrame valid"), VarsFrame.IsValid());
	TestEqual(TEXT("VarsFrame type"), VarsFrame->Type, EDesignNodeType::Frame);
	TestTrue(TEXT("Bound variables metadata captured"), VarsFrame->Metadata.Contains(TEXT("boundVariables")));
	TestTrue(
		TEXT("BoundVariablesJson keeps variable alias id"),
		VarsFrame->Metadata.FindRef(TEXT("boundVariables")).Contains(TEXT("VariableID:color/primary")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserPrivateStringFieldParityTest,
	"Design2UMG.Unit.FigmaParser.PrivateStringFieldParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserPrivateStringFieldParityTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("10:1"), TEXT("LayoutFrame"), TEXT("FRAME"), 0, 0, 120, 80, FString(),
			TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"WRAP\","
				 "\"counterAxisAlignContent\":\"SPACE_BETWEEN\",\"layoutPositioning\":\"ABSOLUTE\","
				 "\"layoutAlign\":\"STRETCH\",\"primaryAxisSizingMode\":\"AUTO\",\"counterAxisSizingMode\":\"FIXED\","
				 "\"gridRowsSizing\":\"auto auto\",\"gridColumnsSizing\":\"1fr 1fr\","
				 "\"gridChildHorizontalAlign\":\"CENTER\",\"gridChildVerticalAlign\":\"MIN\"")),
		*MakeLeafNode(TEXT("10:2"), TEXT("InstanceNode"), TEXT("INSTANCE"), 140, 0, 80, 32,
			TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"set:5678\",\"mainComponentKey\":\"main-key-123\""))),
		TEXT(",\"components\":{},\"componentSets\":{},\"styles\":{}"));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Two parsed root children"), Doc.Children.Num(), 2);
	if (Doc.Children.Num() != 2 || !Doc.Children[0].IsValid() || !Doc.Children[1].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& LayoutNode = Doc.Children[0];
	TestEqual(TEXT("LayoutWrap preserved"), LayoutNode->Metadata.FindRef(TEXT("layoutWrap")), FString(TEXT("WRAP")));
	TestEqual(TEXT("CounterAxisAlignContent preserved"), LayoutNode->Metadata.FindRef(TEXT("counterAxisAlignContent")), FString(TEXT("SPACE_BETWEEN")));
	TestTrue(TEXT("LayoutPositioning normalized"), LayoutNode->LayoutData.bAbsoluteLayoutPositioning);
	TestEqual(TEXT("LayoutAlign preserved"), LayoutNode->Metadata.FindRef(TEXT("layoutAlign")), FString(TEXT("STRETCH")));
	TestEqual(
		TEXT("LayoutAlign is normalized for shared layout decisions"),
		LayoutNode->LayoutData.SelfAlignment.Get(EDesignAxisAlignment::Start),
		EDesignAxisAlignment::Fill);
	TestEqual(TEXT("PrimaryAxisSizingMode preserved"), LayoutNode->Metadata.FindRef(TEXT("primaryAxisSizingMode")), FString(TEXT("AUTO")));
	TestEqual(TEXT("CounterAxisSizingMode preserved"), LayoutNode->Metadata.FindRef(TEXT("counterAxisSizingMode")), FString(TEXT("FIXED")));
	TestEqual(TEXT("GridRowsSizing preserved"), LayoutNode->Metadata.FindRef(TEXT("gridRowsSizing")), FString(TEXT("auto auto")));
	TestEqual(TEXT("GridColumnsSizing preserved"), LayoutNode->Metadata.FindRef(TEXT("gridColumnsSizing")), FString(TEXT("1fr 1fr")));
	TestEqual(TEXT("GridChildHorizontalAlign normalized"), LayoutNode->LayoutData.LayoutChildHorizontalAlign.Get(EDesignAxisAlignment::Start), EDesignAxisAlignment::Center);
	TestEqual(TEXT("GridChildVerticalAlign normalized"), LayoutNode->LayoutData.LayoutChildVerticalAlign.Get(EDesignAxisAlignment::End), EDesignAxisAlignment::Start);

	const TSharedPtr<FDesignNode>& InstanceNode = Doc.Children[1];
	TestTrue(TEXT("Instance flag preserved"), InstanceNode->Source().ComponentInstance.bIsInstance);
	TestEqual(TEXT("ComponentSetId preserved"), InstanceNode->Source().ComponentInstance.ComponentSetId, FString(TEXT("set:5678")));
	TestEqual(TEXT("MainComponentKey preserved"), InstanceNode->Source().ComponentInstance.MainComponentKey, FString(TEXT("main-key-123")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserExplicitGeometryMaskTest,
	"Design2UMG.Unit.FigmaParser.ExplicitGeometryMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserExplicitGeometryMaskTest::RunTest(const FString&)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeContainerNode(TEXT("ellipse-group"), TEXT("Shape A"), TEXT("GROUP"), 0, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("ellipse-mask"), TEXT("Clip A"), TEXT("ELLIPSE"), 0, 0, 100, 80,
				TEXT(",\"isMask\":true")),
			*MakeLeafNode(TEXT("ellipse-content"), TEXT("Layer A"), TEXT("RECTANGLE"), 0, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(1, 0, 0))))),
		*MakeContainerNode(TEXT("rounded-group"), TEXT("Shape B"), TEXT("GROUP"), 120, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("rounded-mask"), TEXT("Clip B"), TEXT("RECTANGLE"), 120, 0, 100, 80,
				TEXT(",\"isMask\":true,\"cornerRadius\":12")),
			*MakeLeafNode(TEXT("rounded-content"), TEXT("Layer B"), TEXT("RECTANGLE"), 120, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0, 1, 0))))),
		*MakeContainerNode(TEXT("mismatch-group"), TEXT("Shape C"), TEXT("GROUP"), 240, 0, 100, 80, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("mismatch-mask"), TEXT("Clip C"), TEXT("ELLIPSE"), 250, 0, 80, 80,
				TEXT(",\"isMask\":true")),
			*MakeLeafNode(TEXT("mismatch-content"), TEXT("Layer C"), TEXT("RECTANGLE"), 240, 0, 100, 80,
				FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0, 0, 1)))))));

	FDesignDocument Doc;
	TestTrue(TEXT("Geometry mask document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("All mask groups remain"), Doc.Children.Num(), 3);
	if (Doc.Children.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("Ellipse mask shape preserved"), Doc.Children[0]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::Ellipse);
	TestEqual(TEXT("Ellipse mask leaf is not emitted as content"), Doc.Children[0]->Children.Num(), 1);
	TestEqual(TEXT("Rounded rectangle mask shape preserved"), Doc.Children[1]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::RoundedRectangle);
	TestTrue(TEXT("Rounded rectangle radius preserved"),
		FMath::IsNearlyEqual(Doc.Children[1]->Visual.GeometryMask.CornerRadius.Uniform, 12.0f));
	TestEqual(TEXT("Mismatched mask is not approximated"), Doc.Children[2]->Visual.GeometryMask.Shape, EDesignGeometryMaskShape::None);
	return true;
}

// ---------------------------------------------------------------------------
// Scroll container stacking axis: explicit auto-layout axis wins over overflow axis
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaScrollAxisLayoutWinsOverOverflowTest,
	"Design2UMG.Unit.FigmaParser.ScrollAxisLayoutWinsOverOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaScrollAxisLayoutWinsOverOverflowTest::RunTest(const FString&)
{
	const auto MakeFrameJson = [](bool bHasLayoutMode, const TCHAR* LayoutMode, const TCHAR* OverflowDirection) -> FString
	{
		FString LayoutField;
		if (bHasLayoutMode)
		{
			LayoutField = FString::Printf(TEXT(",\"layoutMode\":\"%s\",\"itemSpacing\":12"), LayoutMode);
		}
		return FString::Printf(TEXT(R"({
		"document": {"type": "DOCUMENT", "children": [{"type": "CANVAS", "children": [{
			"id": "scroll-frame", "name": "Scroll Frame", "type": "FRAME", "clipsContent": true%s,
			"overflowDirection": "%s",
			"absoluteBoundingBox": {"x": 0, "y": 0, "width": 360, "height": 800},
			"children": [
				{"id": "c1", "name": "Card 1", "type": "FRAME",
				 "absoluteBoundingBox": {"x": 0, "y": 0, "width": 328, "height": 378},
				 "fills": [{"type": "SOLID", "color": {"r": 0, "g": 1, "b": 0, "a": 1}}]},
				{"id": "c2", "name": "Card 2", "type": "FRAME",
				 "absoluteBoundingBox": {"x": 0, "y": 390, "width": 328, "height": 378},
				 "fills": [{"type": "SOLID", "color": {"r": 0, "g": 0, "b": 1, "a": 1}}]}
			]
		}]}]}
	})"), *LayoutField, OverflowDirection);
	};

	struct FCase { bool bHasLayoutMode; const TCHAR* LayoutMode; const TCHAR* Overflow; const TCHAR* ExpectedOrientation; };
	const FCase Cases[] = {
		// Authored VERTICAL stack carrying a HORIZONTAL_SCROLLING overflow: children
		// must keep stacking vertically (Figma renders the layout axis).
		{true, TEXT("VERTICAL"), TEXT("HORIZONTAL_SCROLLING"), TEXT("Vertical")},
		// Authored HORIZONTAL stack carrying a VERTICAL_SCROLLING overflow.
		{true, TEXT("HORIZONTAL"), TEXT("VERTICAL_SCROLLING"), TEXT("Horizontal")},
		// Agreeing axes stay unchanged.
		{true, TEXT("VERTICAL"), TEXT("VERTICAL_SCROLLING"), TEXT("Vertical")},
		{true, TEXT("HORIZONTAL"), TEXT("HORIZONTAL_SCROLLING"), TEXT("Horizontal")},
		// Absolute (no auto-layout) containers keep following the overflow axis.
		{false, nullptr, TEXT("HORIZONTAL_SCROLLING"), TEXT("Horizontal")},
		{false, nullptr, TEXT("VERTICAL_SCROLLING"), TEXT("Vertical")},
	};

	for (const FCase& Case : Cases)
	{
		FDesignDocument Doc;
		if (!TestTrue(TEXT("Scroll-axis fixture parses"),
				FFigmaParser::ParseDocument(MakeFrameJson(Case.bHasLayoutMode, Case.LayoutMode, Case.Overflow), Doc))
			|| Doc.Children.Num() != 1)
		{
			return false;
		}
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());

		const FUMGWidgetNode* ScrollBox = nullptr;
		const auto Search = [&ScrollBox](const FUMGWidgetNode& Node, auto&& Self) -> void
		{
			if (ScrollBox)
			{
				return;
			}
			if (Node.Type == TEXT("ScrollBox"))
			{
				ScrollBox = &Node;
				return;
			}
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				Self(Child, Self);
			}
		};
		Search(Data.RootWidget, Search);

		const FString Label = FString::Printf(TEXT("layout=%s overflow=%s -> ScrollBox found"),
			Case.bHasLayoutMode ? Case.LayoutMode : TEXT("none"), Case.Overflow);
		if (!TestNotNull(Label, ScrollBox))
		{
			return false;
		}
		const FString OrientationLabel = FString::Printf(TEXT("layout=%s overflow=%s -> orientation"),
			Case.bHasLayoutMode ? Case.LayoutMode : TEXT("none"), Case.Overflow);
		TestEqual(OrientationLabel, ScrollBox->ScrollBox.Orientation.Get(FString()), FString(Case.ExpectedOrientation));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Error input
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserErrorInputTest,
	"Design2UMG.Unit.FigmaParser.ErrorInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserErrorInputTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Invalid Figma export JSON"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 2);
	{
		FDesignDocument Doc;
		TestFalse(TEXT("Empty string returns false"), FFigmaParser::ParseDocument(TEXT(""), Doc));
	}
	{
		FDesignDocument Doc;
		TestFalse(TEXT("Malformed JSON returns false"), FFigmaParser::ParseDocument(TEXT("{\"document\":"), Doc));
	}
	{
		AddExpectedError(TEXT("Missing 'document' field"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		TestFalse(TEXT("Missing document field returns false"), FFigmaParser::ParseDocument(TEXT("{\"foo\":1}"), Doc));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaParserReusedOutputTest,
	"Design2UMG.Unit.FigmaParser.ReusedOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserReusedOutputTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("First document parses"), FFigmaParser::ParseDocument(TEXT(R"({"fileKey":"first-file","document":{"id":"first","name":"First","type":"FRAME"}})"), Document))) return false;
	if (!TestTrue(TEXT("Second document parses"), FFigmaParser::ParseDocument(TEXT(R"({"document":{"id":"second","name":"Second","type":"FRAME"}})"), Document))) return false;
	TestEqual(TEXT("Second parse replaces children"), Document.Children.Num(), 1);
	TestFalse(TEXT("Second parse removes old metadata"), Document.Metadata.Contains(TEXT("fileKey")));
	if (Document.Children.Num() == 1) TestEqual(TEXT("Only the second node remains"), Document.Children[0]->Id, FString(TEXT("second")));
	AddExpectedError(TEXT("Missing 'document' field"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Missing document fails"), FFigmaParser::ParseDocument(TEXT("{}"), Document));
	TestEqual(TEXT("Failure clears the output"), Document.Children.Num(), 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExplicitNoWrapTest,
	"Design2UMG.Unit.FigmaParser.ExplicitNoWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExplicitNoWrapTest::RunTest(const FString&)
{
	TArray<FString> Children;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FString Id = FString::Printf(TEXT("item-%d"), Index);
		Children.Add(MakeLeafNode(*Id, *Id, TEXT("RECTANGLE"), Index * 30, Index % 2 == 0 ? 0 : 10,
			30, Index % 2 == 0 ? 40 : 20, TEXT(",\"layoutSizingHorizontal\":\"FILL\"")));
	}
	const FString Parent = MakeContainerNode(TEXT("row"), TEXT("Row"), TEXT("FRAME"), 0, 0, 180, 40,
		FString::Join(Children, TEXT(",")), TEXT(",\"layoutMode\":\"HORIZONTAL\",\"layoutWrap\":\"NO_WRAP\",\"counterAxisAlignItems\":\"CENTER\""));
	FDesignDocument Doc;
	if (!TestTrue(TEXT("No-wrap document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Parent), Doc))) return false;
	TestEqual(TEXT("Explicit NO_WRAP wins over child Y differences"), Doc.Children[0]->LayoutData.Layout, EDesignLayout::Horizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNonSolidStrokeTest,
	"Design2UMG.Unit.FigmaParser.NonSolidStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNonSolidStrokeTest::RunTest(const FString&)
{
	const FString Gradient = TEXT("{\"type\":\"GRADIENT_LINEAR\",\"gradientStops\":[{\"position\":0,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":1,\"b\":0,\"a\":1}}]}");
	FDesignDocument Doc;
	const FString Shape = MakeLeafNode(TEXT("shape"), TEXT("Shape"), TEXT("RECTANGLE"), 0, 0, 100, 100,
		FString::Printf(TEXT(",\"strokeWeight\":2,\"strokes\":[%s],\"strokeGeometry\":[{\"path\":\"M0 0 L100 0 L100 100 L0 100 Z M2 2 L2 98 L98 98 L98 2 Z\",\"windingRule\":\"NONZERO\"}]"), *Gradient));
	if (!TestTrue(TEXT("Gradient stroke document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(Shape), Doc))) return false;
	TestTrue(TEXT("Gradient stroke retains its thickness"), Doc.Children[0]->Visual.Stroke.HasStroke());
	if (TestTrue(TEXT("Gradient stroke retains its paint"), Doc.Children[0]->Visual.Stroke.Fill.IsSet()))
	{
		TestTrue(TEXT("Stroke paint stays gradient, not black"), Doc.Children[0]->Visual.Stroke.Fill->Type == EDesignFillType::Gradient);
		TestEqual(TEXT("Stroke retains both gradient stops"), Doc.Children[0]->Visual.Stroke.Fill->Gradient.Colors.Num(), 2);
	}
	FUMGWidgetNode Surface;
	Surface.Type = TEXT("Border");
	Surface.Id = TEXT("shape");
	Surface.Name = TEXT("Shape");
	const FUMGWidgetNode Painted = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Surface), Doc.Children[0]);
	TestEqual(TEXT("Stroke-only shape remains one native image"), Painted.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Stroke-only shape adds no wrappers"), Painted.Children.IsEmpty());
	TestTrue(TEXT("A uniform primitive uses its canonical contour"), Painted.Path.Geometry.IsSet());
	TestEqual(TEXT("Canonical primitive uses the declared stroke width"), Painted.Path.StrokeThickness.Get(0.0f), 2.0f);
	TestFalse(TEXT("Canonical paint does not inherit source region bounds"), Painted.Path.SourcePaintGeometry.IsSet());
	TestEqual(TEXT("Original gradient stops reach the native resource"), Painted.Brush.Gradient.Stops.Num(), 2);
	const TSharedPtr<FDesignNode> Line = MakeShared<FDesignNode>(*Doc.Children[0]);
	Line->Type = EDesignNodeType::Line;
	Line->Shape.PaintGeometry.StrokePaths.Reset();
	Line->Transform.SetWidthValue(FDesignSize::Fixed(100));
	Line->Transform.SetHeightValue(FDesignSize::Fixed(0));
	Line->Visual.Stroke.Thickness.Uniform = 10;
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Circular;
	Line->Visual.Stroke.Fill->Gradient.StartPoint = FVector2D(0.1f, 0.0f);
	Line->Visual.Stroke.Fill->Gradient.EndPoint = FVector2D(0.9f, 0.0f);
	FUMGWidgetNode LineWidget;
	LineWidget.Type = TEXT("Image");
	LineWidget.Bounds.W = 100;
	const FUMGWidgetNode PaintedLine = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(LineWidget), Line);
	TestEqual(TEXT("Cap painting does not change the layout width"), PaintedLine.WidgetStyle.WidthOverride.Get(0), 100.0f);
	TestEqual(TEXT("Cap padding shifts the centerline inside its canvas"), PaintedLine.Path.Geometry.Get(FString()), FString(TEXT("M5.000000 5.000000 L105.000000 5.000000")));
	TestTrue(TEXT("Native render expansion retains the authored centerline length"), PaintedLine.Render.RenderTransformScale.Get(FVector2D(1, 1)).Equals(FVector2D(1.1, 1), 0.0001));
	TestTrue(TEXT("A stroke-only line stays one native image"), PaintedLine.Type == TEXT("Image") && PaintedLine.Children.IsEmpty());
	TestTrue(TEXT("Independent canvas includes both half-stroke caps"), PaintedLine.Path.CanvasSize.Get(FVector2D::ZeroVector).Equals(FVector2D(110, 10)));
	TestTrue(TEXT("Gradient start keeps its authored line coordinate"), FMath::IsNearlyEqual(PaintedLine.Brush.Gradient.StartPoint.GetValue().X * 110.0f - 5.0f, 10.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Gradient end keeps its authored line coordinate"), FMath::IsNearlyEqual(PaintedLine.Brush.Gradient.EndPoint.GetValue().X * 110.0f - 5.0f, 90.0f, KINDA_SMALL_NUMBER));
	FUMGWidgetNode TransformedLine;
	TransformedLine.Type = TEXT("Image");
	TransformedLine.Bounds.W = 100;
	TransformedLine.Render.RenderTransformScale = FVector2D(2, 3);
	TransformedLine.Render.RenderAngle = 30.0f;
	TransformedLine.Render.RenderTransformPivot = FVector2D(0.25, 0.75);
	TransformedLine.Render.RenderTranslation = FVector2D(7, 9);
	const FUMGWidgetNode TransformedPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(TransformedLine), Line);
	TestTrue(TEXT("Ink expansion composes with authored scale"), TransformedPaint.Render.RenderTransformScale.GetValue().Equals(FVector2D(2.2, 3), 0.0001));
	TestEqual(TEXT("Authored rotation survives"), TransformedPaint.Render.RenderAngle.GetValue(), 30.0f);
	TestTrue(TEXT("Authored pivot survives"), TransformedPaint.Render.RenderTransformPivot.GetValue().Equals(FVector2D(0.25, 0.75)));
	TestTrue(TEXT("Noncentral pivot correction follows the authored rotation and scale"), TransformedPaint.Render.RenderTranslation.GetValue().Equals(FVector2D(7.0 - 5.0 * FMath::Cos(PI / 6), 1.5), 0.0001));
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Square;
	Line->Children.Add(MakeShared<FDesignNode>());
	FUMGWidgetNode ContentLine;
	ContentLine.Type = TEXT("Border");
	ContentLine.Bounds.W = 100;
	const FUMGWidgetNode ContentPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ContentLine), Line);
	TestTrue(TEXT("Line content retains the existing native overlay and two layers"), ContentPaint.Type == TEXT("Overlay") && ContentPaint.Children.Num() == 2);
	if (ContentPaint.Children.Num() == 2)
	{
		const FUMGWidgetNode& CapLayer = ContentPaint.Children[1];
		TestEqual(TEXT("Square caps do not widen the overlay's layout"), ContentPaint.Bounds.W, 100.0f);
		TestEqual(TEXT("Square cap image keeps the original desired width"), CapLayer.WidgetStyle.WidthOverride.GetValue(), 100.0f);
		TestTrue(TEXT("Square caps reserve the same half-stroke canvas extent"), CapLayer.Path.CanvasSize.GetValue().Equals(FVector2D(110, 10)));
	}
	Line->Children.Empty();
	Line->Visual.Stroke.Cap = EDesignStrokeCap::Butt;
	FUMGWidgetNode ButtLine;
	ButtLine.Type = TEXT("Image");
	const FUMGWidgetNode ButtPaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ButtLine), Line);
	TestTrue(TEXT("Butt caps add no horizontal extent"), ButtPaint.Path.CanvasSize.GetValue().Equals(FVector2D(100, 10)));

	Doc.Children[0]->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	Doc.Children[0]->Visual.Fills.Add(Doc.Children[0]->Visual.Stroke.Fill.GetValue());
	FUMGWidgetNode Filled;
	Filled.Type = TEXT("Border");
	Filled.Id = TEXT("filled");
	Filled.Name = TEXT("Filled");
	Filled.Brush.Gradient = Painted.Brush.Gradient;
	Filled.Brush.BrushColor = FLinearColor::White;
	Filled.Brush.OutlineThickness = 2.0f;
	Filled.WidgetStyle.StrokePadding = FMargin(1.0f);
	const FUMGWidgetNode PaintedBorder = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Filled), Doc.Children[0]);
	TestEqual(TEXT("An inside primitive retains its combined native surface"), PaintedBorder.Type, FString(TEXT("Border")));
	TestTrue(TEXT("Combined primitive paint adds no source layers"), PaintedBorder.Children.IsEmpty());
	TestEqual(TEXT("Combined primitive keeps the declared width"), PaintedBorder.Path.StrokeThickness.Get(0.0f), 2.0f);
	TestFalse(TEXT("Combined primitive has no source region bounds"), PaintedBorder.Path.SourcePaintGeometry.IsSet());
	TestTrue(TEXT("Unselected source geometry does not clear native padding"), PaintedBorder.WidgetStyle.StrokePadding.IsSet());
	auto ImageNode = MakeShared<FDesignNode>(*Doc.Children[0]);
	ImageNode->Visual.Fills.Reset();
	FDesignFill ImageFill;
	ImageFill.Type = EDesignFillType::Image;
	ImageFill.Image.Url = TEXT("/Textures/fill.png");
	ImageNode->Visual.Fills.Add(ImageFill);
	FUMGWidgetNode ImageSurface;
	ImageSurface.Type = TEXT("Border");
	ImageSurface.Brush.BrushColor = FLinearColor(1, 1, 1, 0.4f);
	ImageSurface.Brush.Image.TexturePath = ImageFill.Image.Url;
	ImageSurface.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData();
	ImageSurface.Brush.Image.Transform->TX = 0.25f;
	const FUMGWidgetNode ImagePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(ImageSurface), ImageNode);
	TestEqual(TEXT("Combined primitive preserves fill alpha"), ImagePaint.Path.FillColor.Get(FLinearColor::Transparent).A, 0.4f);
	TestEqual(TEXT("Combined primitive preserves image resource"), ImagePaint.Brush.Image.TexturePath.Get(FString()), ImageFill.Image.Url);
	TestTrue(TEXT("Combined primitive preserves authored image coordinates"), ImagePaint.Brush.Image.Transform.IsSet()
		&& ImagePaint.Brush.Image.Transform->TX == 0.25f);
	const auto Frame = MakeShared<FDesignNode>(*Doc.Children[0]);
	Frame->Type = EDesignNodeType::Frame;
	Frame->Shape.PaintGeometry.StrokePaths.Reset();
	Frame->Visual.Stroke.Align = EDesignStrokeAlign::Center;
	Frame->Visual.Fills.Reset();
	FUMGWidgetNode FrameSurface;
	FrameSurface.Type = TEXT("Border");
	const FUMGWidgetNode FramePaint = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(FrameSurface), Frame);
	TestTrue(TEXT("A frame derives its real rounded contour without exported stroke geometry"),
		FramePaint.Path.Geometry.IsSet() && FramePaint.Brush.Gradient.Stops.Num() == 2);

	AddExpectedError(TEXT("FigmaParser: Unsupported stroke paint"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString ImageShape = MakeLeafNode(TEXT("image-shape"), TEXT("ImageShape"), TEXT("RECTANGLE"), 0, 0, 100, 100,
		TEXT(",\"strokeWeight\":2,\"strokes\":[{\"type\":\"IMAGE\",\"imageRef\":\"test-image\"}]"));
	if (!TestTrue(TEXT("Image stroke document parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(ImageShape), Doc))) return false;
	TestFalse(TEXT("Unsupported image paint is not fabricated as black"), Doc.Children[0]->Visual.Stroke.HasStroke());

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaAbsoluteFlowFitSizeTest,
	"Design2UMG.Unit.FigmaParser.AbsoluteFlowFitSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaAbsoluteFlowFitSizeTest::RunTest(const FString&)
{
	const TSharedPtr<FDesignNode> Parent = MakeShared<FDesignNode>();
	Parent->Type = EDesignNodeType::Frame;
	Parent->Transform.SetWidthValue(FDesignSize::Fit(68));
	Parent->Transform.SetHeightValue(FDesignSize::Fixed(80));
	Parent->LayoutData.Layout = EDesignLayout::Horizontal;
	Parent->LayoutData.SetGap(8);
	FDesignPadding Padding;
	Padding.Left = Padding.Right = 10;
	Parent->LayoutData.SetPadding(Padding);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TSharedPtr<FDesignNode> Child = MakeShared<FDesignNode>();
		Child->Type = EDesignNodeType::Rectangle;
		Child->Transform.SetWidthValue(FDesignSize::Fixed(Index == 1 ? 1000 : 20));
		Child->Transform.SetHeightValue(FDesignSize::Fixed(20));
		Child->LayoutData.bAbsoluteLayoutPositioning = Index == 1;
		Child->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#FF0000")));
		Parent->Children.Add(Child);
	}
	TestEqual(TEXT("Absolute child contributes no flow fit width"), SizeEstimator::EstimateNodeSize(Parent, 0.0f).X, 68.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
