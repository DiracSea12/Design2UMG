// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignReferenceExpansion.h"
#include "DesignTreeLimits.h"
#include "DesignSchema.h"
#include "Design2UMGConverter.h"
#include "SizeEstimator.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "FigmaTestDocBuilder.h"

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
		[&Doc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, Doc); });
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
					 "\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"offset\":{\"x\":0,\"y\":0},\"radius\":40}],"
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
			TEXT(",\"fills\":[],\"effects\":[]")));

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
	{
		AddExpectedError(TEXT("Invalid Figma export JSON"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		TestFalse(TEXT("Empty string returns false"), FFigmaParser::ParseDocument(TEXT(""), Doc));
	}
	{
		AddExpectedError(TEXT("Invalid Figma export JSON"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 1);
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

#endif // WITH_DEV_AUTOMATION_TESTS
