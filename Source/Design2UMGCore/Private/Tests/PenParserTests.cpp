// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PenBundledLibraries.h"
#include "PenParser.h"
#include "RefExpander.h"
#include "DesignSchema.h"
#include "SizeEstimator.h"
#include "DesignTreeBudget.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	static const TCHAR* PluginName = TEXT("Design2UMG");
	static const FString FixtureBase = TEXT("Source/Design2UMGCore/Tests/Fixtures/pen");

	bool LoadPenFixture(FAutomationTestBase& Test, const FString& FileName, FDesignDocument& OutDoc)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), FixtureBase, FileName);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *Path));
			return false;
		}
		return FPenParser::ParseDocument(Json, OutDoc);
	}

	void ExpectSize(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FDesignSize& Actual,
		const EDesignSizeMode ExpectedMode,
		const float ExpectedValue)
	{
		Test.TestEqual(FString::Printf(TEXT("%s mode"), What), Actual.Mode, ExpectedMode);
		Test.TestEqual(FString::Printf(TEXT("%s value"), What), Actual.Value, ExpectedValue);
	}

	void ExpectKeywordFallbackSize(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FDesignSize& Actual,
		const EDesignSizeMode ExpectedMode)
	{
		Test.TestEqual(FString::Printf(TEXT("%s mode"), What), Actual.Mode, ExpectedMode);
		Test.TestEqual(FString::Printf(TEXT("%s value"), What), Actual.Value, 0.0f);
	}

}

// ---------------------------------------------------------------------------
// Basic node parsing: frame / text / rectangle
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserBasicNodesTest,
	"Design2UMG.Unit.PenParser.BasicNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserBasicNodesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("basic_nodes.json"), Doc))
		return false;

	TestEqual(TEXT("One root child"), Doc.Children.Num(), 1);
	const TSharedPtr<FDesignNode>& Frame = Doc.Children[0];
	TestTrue(TEXT("Frame valid"), Frame.IsValid());
	TestEqual(TEXT("Frame id"), Frame->Id, FString(TEXT("frame-1")));
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

// ---------------------------------------------------------------------------
// Auto-layout: horizontal / vertical / wrap with gap and padding
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserAutoLayoutTest,
	"Design2UMG.Unit.PenParser.AutoLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserAutoLayoutTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("autolayout.json"), Doc))
		return false;

	TestEqual(TEXT("Three root children"), Doc.Children.Num(), 3);

	// Horizontal
	const TSharedPtr<FDesignNode>& HBox = Doc.Children[0];
	TestTrue(TEXT("HBox valid"), HBox.IsValid());
	TestEqual(TEXT("HBox layout"), HBox->LayoutData.Layout, EDesignLayout::Horizontal);
	TestTrue(TEXT("HBox has gap"), HBox->LayoutData.ExplicitGap.IsSet());
	TestEqual(TEXT("HBox gap value"), HBox->LayoutData.ExplicitGap.GetValue(), 8.0f);
	TestTrue(TEXT("HBox has padding"), HBox->LayoutData.ExplicitPadding.IsSet());
	TestEqual(TEXT("HBox padding left"), HBox->LayoutData.ExplicitPadding.GetValue().Left, 8.0f);

	// Vertical
	const TSharedPtr<FDesignNode>& VBox = Doc.Children[1];
	TestTrue(TEXT("VBox valid"), VBox.IsValid());
	TestEqual(TEXT("VBox layout"), VBox->LayoutData.Layout, EDesignLayout::Vertical);
	TestTrue(TEXT("VBox has gap"), VBox->LayoutData.ExplicitGap.IsSet());
	TestEqual(TEXT("VBox gap value"), VBox->LayoutData.ExplicitGap.GetValue(), 12.0f);

	// Wrap
	const TSharedPtr<FDesignNode>& Wrap = Doc.Children[2];
	TestTrue(TEXT("Wrap valid"), Wrap.IsValid());
	TestEqual(TEXT("Wrap layout"), Wrap->LayoutData.Layout, EDesignLayout::Wrap);

	return true;
}

// ---------------------------------------------------------------------------
// Text node: fontFamily / fontSize / textAlign / textGrowth
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserTextNodeTest,
	"Design2UMG.Unit.PenParser.TextNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserTextNodeTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("text_node.json"), Doc))
		return false;

	TestEqual(TEXT("One root child"), Doc.Children.Num(), 1);
	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestTrue(TEXT("Node valid"), Node.IsValid());
	TestEqual(TEXT("FontFamily"), Node->Text.FontFamily, FString(TEXT("Inter")));
	TestEqual(TEXT("FontSize"), Node->Text.FontSize, 24.0f);
	TestEqual(TEXT("TextAlign"), Node->Text.TextAlign, EDesignTextAlign::Center);
	TestEqual(TEXT("TextGrowth"), Node->Text.TextGrowth, EDesignTextGrowth::FixedWidthHeight);
	TestEqual(TEXT("Missing Pencil line height remains zero"), Node->Text.LineHeight, 0.0f);
	TestTrue(TEXT("Pencil text uses half-leading baseline placement"), Node->Text.bSourceHalfLeadingBaseline);
	TestTrue(TEXT("Pencil text retains its source line-height rounding rule"), Node->Text.bSourceRoundLineHeightToPixels);

	const FString ExplicitLineHeightJson = TEXT(R"JSON(
{
  "version": "2.14",
  "children": [{
    "id": "explicit-line-height",
    "name": "Explicit Line Height",
    "type": "text",
    "content": "Explicit",
    "fontSize": 16,
    "lineHeight": 1.0
  }]
}
)JSON");
	FDesignDocument ExplicitDoc;
	TestTrue(TEXT("Explicit Pencil line height fixture parses"), FPenParser::ParseDocument(ExplicitLineHeightJson, ExplicitDoc));
	TestEqual(TEXT("Explicit Pencil line height fixture has one node"), ExplicitDoc.Children.Num(), 1);
	if (ExplicitDoc.Children.Num() == 1 && ExplicitDoc.Children[0].IsValid())
	{
		TestEqual(TEXT("Explicit Pencil line height is preserved"), ExplicitDoc.Children[0]->Text.LineHeight, 1.0f);
		TestTrue(TEXT("Explicit Pencil line height retains half-leading placement"), ExplicitDoc.Children[0]->Text.bSourceHalfLeadingBaseline);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserIconNodeFormatsTest,
	"Design2UMG.Unit.PenParser.IconNodeFormats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserIconNodeFormatsTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON(
{
  "version": "2.13",
  "children": [
    {
      "id": "current-icon",
      "name": "Current Icon",
      "type": "icon",
      "icon": "search",
      "library": "lucide",
      "width": 24,
      "height": 24
    },
    {
      "id": "legacy-icon",
      "name": "Legacy Icon",
      "type": "icon_font",
      "iconFontName": "search",
      "iconFontFamily": "lucide"
    }
  ]
}
)JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Pen icon formats parse"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Root count"), Doc.Children.Num(), 2);
	if (Doc.Children.Num() != 2)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& CurrentIcon = Doc.Children[0];
	TestTrue(TEXT("Current icon valid"), CurrentIcon.IsValid());
	TestEqual(TEXT("Current icon type"), CurrentIcon->Type, EDesignNodeType::IconFont);
	TestEqual(TEXT("Current icon name"), CurrentIcon->Text.IconFontName, FString(TEXT("search")));
	TestEqual(TEXT("Current icon family"), CurrentIcon->Text.IconFontFamily, FString(TEXT("lucide")));
	const TSharedPtr<FDesignNode>& UnsupportedIcon = Doc.Children[1];
	TestTrue(TEXT("Unsupported icon node remains structurally present"), UnsupportedIcon.IsValid());
	TestEqual(TEXT("Unsupported icon type is rejected"), UnsupportedIcon->Type, EDesignNodeType::Unknown);
	TestTrue(TEXT("Unsupported icon name is not migrated"), UnsupportedIcon->Text.IconFontName.IsEmpty());
	TestTrue(TEXT("Unsupported icon family is not migrated"), UnsupportedIcon->Text.IconFontFamily.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSizeEstimatorFitContentLeafMinimumGrowthTest,
	"Design2UMG.Unit.SizeEstimator.FitContentLeafMinimumGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSizeEstimatorFitContentLeafMinimumGrowthTest::RunTest(const FString&)
{
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("fit-text");
	TextNode->Name = TEXT("Fit Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.Content = TEXT("Fit content grows to measured text");
	TextNode->Text.FontSize = 32.0f;
	TextNode->Transform.Width = FDesignSize::Fit(4.0f);
	TextNode->Transform.Height = FDesignSize::Fit(4.0f);

	FDesignDocument EmptyDoc;
	const FVector2D TextSize = SizeEstimator::PromoteFitContentSizes(TextNode, EmptyDoc,
		[&EmptyDoc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, EmptyDoc); });
	TestTrue(TEXT("Text fit width grows beyond its declared minimum"), TextSize.X > 4.0f);
	TestTrue(TEXT("Text fit height grows beyond its declared minimum"), TextSize.Y > 4.0f);
	TestTrue(TEXT("Text node stores the grown fit width"), TextNode->Transform.Width.Value > 4.0f);
	TestTrue(TEXT("Text node stores the grown fit height"), TextNode->Transform.Height.Value > 4.0f);

	TSharedPtr<FDesignNode> IconNode = MakeShared<FDesignNode>();
	IconNode->Id = TEXT("fit-icon");
	IconNode->Name = TEXT("Fit Icon");
	IconNode->Type = EDesignNodeType::IconFont;
	IconNode->Text.Content = TEXT("search");
	IconNode->Text.IconFontName = TEXT("search");
	IconNode->Text.FontSize = 32.0f;
	IconNode->Transform.Width = FDesignSize::Fit(4.0f);
	IconNode->Transform.Height = FDesignSize::Fit(4.0f);

	const FVector2D IconSize = SizeEstimator::PromoteFitContentSizes(IconNode, EmptyDoc,
		[&EmptyDoc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, EmptyDoc); });
	TestEqual(TEXT("Icon fit width grows to its measured content"), IconSize.X, 32.0);
	TestEqual(TEXT("Icon fit height grows to its measured content"), IconSize.Y, 32.0);
	TestEqual(TEXT("Icon node stores the grown fit width"), IconNode->Transform.Width.Value, 32.0f);
	TestEqual(TEXT("Icon node stores the grown fit height"), IconNode->Transform.Height.Value, 32.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserStrokeNodeFormatsTest,
	"Design2UMG.Unit.PenParser.StrokeNodeFormats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserStrokeNodeFormatsTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON(
{
  "version": "2.13",
  "children": [
    {
      "id": "current-stroke",
      "name": "Current Stroke",
      "type": "frame",
		"stroke": "#112233",
		"strokeWidth": { "top": 1, "right": 2, "bottom": 3, "left": 4 },
		"strokeAlignment": "outer",
		"strokeLinecap": "round",
		"strokeLinejoin": "bevel"
    },
    {
      "id": "current-inner-stroke",
      "name": "Current Inner Stroke",
      "type": "frame",
      "stroke": "#445566",
      "strokeWidth": 2,
      "strokeAlignment": "inner"
    },
    {
      "id": "malformed-thickness",
      "name": "Malformed Thickness",
      "type": "frame",
      "strokeWidth": { "unexpected": 3 }
    },
    {
      "id": "legacy-nested-stroke",
      "name": "Legacy Nested Stroke",
      "type": "frame",
      "stroke": {
        "fill": "#778899",
        "thickness": 3,
        "align": "center",
        "join": "round",
        "miterAngle": 45,
        "cap": "square",
        "dashPattern": [2, 4]
      }
    }
  ]
}
)JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Pen stroke formats parse"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Root count"), Doc.Children.Num(), 4);
	if (Doc.Children.Num() != 4)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Current = Doc.Children[0];
	const TSharedPtr<FDesignNode>& CurrentInner = Doc.Children[1];
	const TSharedPtr<FDesignNode>& MalformedThickness = Doc.Children[2];
	const TSharedPtr<FDesignNode>& UnsupportedNestedStroke = Doc.Children[3];
	TestTrue(TEXT("Current stroke valid"), Current.IsValid());
	TestTrue(TEXT("Current inner stroke valid"), CurrentInner.IsValid());
	TestTrue(TEXT("Malformed thickness node valid"), MalformedThickness.IsValid());
	TestTrue(TEXT("Unsupported nested stroke node valid"), UnsupportedNestedStroke.IsValid());
	if (!Current.IsValid() || !CurrentInner.IsValid() || !MalformedThickness.IsValid() || !UnsupportedNestedStroke.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Current align"), Current->Visual.Stroke.Align, EDesignStrokeAlign::Outside);
	TestEqual(TEXT("Current non-uniform thickness"), Current->Visual.Stroke.Thickness.bUniform, false);
	TestEqual(TEXT("Current top thickness"), Current->Visual.Stroke.Thickness.Top, 1.0f);
	TestEqual(TEXT("Current right thickness"), Current->Visual.Stroke.Thickness.Right, 2.0f);
	TestEqual(TEXT("Current bottom thickness"), Current->Visual.Stroke.Thickness.Bottom, 3.0f);
	TestEqual(TEXT("Current left thickness"), Current->Visual.Stroke.Thickness.Left, 4.0f);
	TestEqual(TEXT("Current cap is normalized for shared conversion"), Current->Visual.Stroke.Cap, EDesignStrokeCap::Circular);
	TestEqual(TEXT("Current join is normalized for shared conversion"), Current->Visual.Stroke.Join, EDesignStrokeJoin::Bevel);
	TestTrue(TEXT("Current fill set"), Current->Visual.Stroke.Fill.IsSet());
	if (Current->Visual.Stroke.Fill.IsSet())
	{
		TestEqual(TEXT("Current stroke color"), Current->Visual.Stroke.Fill.GetValue().SolidColor, FString(TEXT("#112233")));
	}

	TestEqual(TEXT("Current inner align maps to inside"), CurrentInner->Visual.Stroke.Align, EDesignStrokeAlign::Inside);
	TestEqual(TEXT("Current inner uniform thickness"), CurrentInner->Visual.Stroke.Thickness.bUniform, true);
	TestEqual(TEXT("Current inner thickness"), CurrentInner->Visual.Stroke.Thickness.Uniform, 2.0f);
	TestTrue(TEXT("Current inner fill set"), CurrentInner->Visual.Stroke.Fill.IsSet());
	if (CurrentInner->Visual.Stroke.Fill.IsSet())
	{
		TestEqual(TEXT("Current inner color"), CurrentInner->Visual.Stroke.Fill.GetValue().SolidColor, FString(TEXT("#445566")));
	}
	TestTrue(TEXT("Malformed thickness object keeps the default uniform representation"),
		MalformedThickness->Visual.Stroke.Thickness.bUniform);
	TestFalse(TEXT("Malformed thickness object does not fabricate a stroke"),
		MalformedThickness->Visual.Stroke.HasStroke());
	TestFalse(TEXT("Unsupported nested stroke is not parsed"), UnsupportedNestedStroke->Visual.Stroke.HasStroke());
	TestFalse(TEXT("Legacy join is not migrated to metadata"), UnsupportedNestedStroke->Metadata.Contains(TEXT("stroke.join")));
	TestFalse(TEXT("Legacy miter angle is not migrated to metadata"), UnsupportedNestedStroke->Metadata.Contains(TEXT("stroke.miterAngle")));
	TestFalse(TEXT("Legacy cap is not migrated to metadata"), UnsupportedNestedStroke->Metadata.Contains(TEXT("stroke.cap")));
	TestFalse(TEXT("Legacy dash pattern is not migrated to metadata"), UnsupportedNestedStroke->Metadata.Contains(TEXT("stroke.dashPattern")));

	return true;
}

// ---------------------------------------------------------------------------
// Error / empty input
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserErrorInputTest,
	"Design2UMG.Unit.PenParser.ErrorInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserErrorInputTest::RunTest(const FString&)
{
	// Empty JSON string
	{
		AddExpectedError(TEXT("Invalid design file format"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		const bool bResult = FPenParser::ParseDocument(TEXT(""), Doc);
		TestFalse(TEXT("Empty string returns false"), bResult);
	}

	// Invalid JSON
	{
		AddExpectedError(TEXT("Invalid design file format"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		const bool bResult = FPenParser::ParseDocument(TEXT("{not valid json"), Doc);
		TestFalse(TEXT("Invalid JSON returns false"), bResult);
	}

	// Valid JSON but no children — should succeed (version defaults, empty children)
	{
		FDesignDocument Doc;
		const bool bResult = FPenParser::ParseDocument(TEXT("{\"version\":\"1.0\"}"), Doc);
		TestTrue(TEXT("No-children JSON succeeds"), bResult);
		TestEqual(TEXT("No children"), Doc.Children.Num(), 0);
	}

	// Missing required field on child node: parser keeps the document but drops the bad child.
	{
		AddExpectedError(TEXT("without id"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		const bool bResult = FPenParser::ParseDocument(TEXT("{\"version\":\"1.0\",\"children\":[{\"type\":\"frame\",\"name\":\"MissingId\"}]}"), Doc);
		TestTrue(TEXT("Missing-id child does not fail whole document"), bResult);
		TestEqual(TEXT("Missing-id child skipped"), Doc.Children.Num(), 0);
	}

	// Circular ref is rejected by ref expansion instead of crashing conversion.
	{
		const FString CircularRefJson =
			TEXT("{")
			TEXT("\"version\":\"1.0\",")
			TEXT("\"children\":[")
			TEXT("{\"id\":\"comp-a\",\"name\":\"CompA\",\"type\":\"ref\",\"ref\":\"comp-b\",\"reusable\":true},")
			TEXT("{\"id\":\"comp-b\",\"name\":\"CompB\",\"type\":\"ref\",\"ref\":\"comp-a\",\"reusable\":true},")
			TEXT("{\"id\":\"entry\",\"name\":\"Entry\",\"type\":\"ref\",\"ref\":\"comp-a\"}")
			TEXT("]}")
			;

		FDesignDocument Doc;
		TestTrue(TEXT("Circular-ref document parses"), FPenParser::ParseDocument(CircularRefJson, Doc));
		TestEqual(TEXT("Circular-ref roots parsed"), Doc.Children.Num(), 3);
		AddExpectedError(TEXT("circular ref detected"), EAutomationExpectedErrorFlags::Contains, 1);
		const TSharedPtr<FDesignNode> Expanded = RefExpander::ExpandRef(Doc.Children[2], Doc);
		TestFalse(TEXT("Circular ref expansion returns null"), Expanded.IsValid());
	}

	// A ref instance that omits height must keep the reusable component's authored fit-content minimum.
	{
		const FString RefDefaultFitSizeJson =
			TEXT("{")
			TEXT("\"version\":\"1.0\",")
			TEXT("\"children\":[")
			TEXT("{\"id\":\"row-template\",\"name\":\"RowTemplate\",\"type\":\"frame\",\"reusable\":true,\"height\":\"fit_content(44)\"},")
			TEXT("{\"id\":\"row-instance\",\"name\":\"RowInstance\",\"type\":\"ref\",\"ref\":\"row-template\"}")
			TEXT("]}")
			;

		FDesignDocument Doc;
		TestTrue(TEXT("Default-fit ref document parses"), FPenParser::ParseDocument(RefDefaultFitSizeJson, Doc));
		TestEqual(TEXT("Default-fit ref roots parsed"), Doc.Children.Num(), 2);
		const TSharedPtr<FDesignNode> Expanded = RefExpander::ExpandRef(Doc.Children[1], Doc);
		TestTrue(TEXT("Default-fit ref expansion returns node"), Expanded.IsValid());
		if (Expanded.IsValid())
		{
			ExpectSize(*this, TEXT("Default-fit ref keeps target height"), Expanded->Transform.Height, EDesignSizeMode::FitContent, 44.0f);
		}
	}

	// Padding and its runtime ownership form one semantic override.
	{
		const FString RefPaddingPolicyJson =
			TEXT("{")
			TEXT("\"version\":\"1.0\",")
			TEXT("\"children\":[")
			TEXT("{\"id\":\"panel-template\",\"name\":\"PanelTemplate\",\"type\":\"frame\",\"reusable\":true,\"layout\":\"vertical\",\"padding\":8,\"children\":[{\"id\":\"body\",\"name\":\"Body\",\"type\":\"frame\"}]} ,")
			TEXT("{\"id\":\"panel-instance\",\"name\":\"PanelInstance\",\"type\":\"ref\",\"ref\":\"panel-template\",\"padding\":24,\"children\":[{\"id\":\"positioned\",\"name\":\"Positioned\",\"type\":\"frame\",\"x\":24,\"y\":24}]},")
			TEXT("{\"id\":\"panel-descendant-instance\",\"name\":\"PanelDescendantInstance\",\"type\":\"ref\",\"ref\":\"panel-template\",\"descendants\":{\"body\":{\"id\":\"body\",\"type\":\"frame\",\"padding\":12,\"children\":[{\"id\":\"nested-positioned\",\"name\":\"NestedPositioned\",\"type\":\"frame\",\"x\":12,\"y\":12}]}}}")
			TEXT("]}");

		FDesignDocument Doc;
		TestTrue(TEXT("Ref padding policy document parses"), FPenParser::ParseDocument(RefPaddingPolicyJson, Doc));
		TestEqual(TEXT("Ref padding policy roots parsed"), Doc.Children.Num(), 3);
		const TSharedPtr<FDesignNode> Expanded = RefExpander::ExpandRef(Doc.Children[1], Doc);
		TestTrue(TEXT("Ref padding policy expansion returns node"), Expanded.IsValid());
		if (Expanded.IsValid())
		{
			TestTrue(TEXT("Instance padding is explicit"), Expanded->LayoutData.ExplicitPadding.IsSet());
			TestTrue(TEXT("Instance padding policy is explicit"), Expanded->LayoutData.PaddingRequiresRuntimeWrap.IsSet());
			TestFalse(TEXT("Instance padding already baked into explicit child coordinates"), Expanded->LayoutData.PaddingRequiresRuntimeWrap.GetValue());
		}

		const TSharedPtr<FDesignNode> DescendantExpanded = RefExpander::ExpandRef(Doc.Children[2], Doc);
		TestTrue(TEXT("Descendant padding policy expansion returns node"), DescendantExpanded.IsValid());
		if (DescendantExpanded.IsValid())
		{
			TestEqual(TEXT("Expanded template child count"), DescendantExpanded->Children.Num(), 1);
			if (DescendantExpanded->Children.Num() == 1 && DescendantExpanded->Children[0].IsValid())
			{
				const TSharedPtr<FDesignNode>& Body = DescendantExpanded->Children[0];
				TestTrue(TEXT("Descendant padding is explicit"), Body->LayoutData.ExplicitPadding.IsSet());
				TestTrue(TEXT("Descendant padding policy is explicit"), Body->LayoutData.PaddingRequiresRuntimeWrap.IsSet());
				TestFalse(TEXT("Descendant padding already baked into explicit child coordinates"), Body->LayoutData.PaddingRequiresRuntimeWrap.GetValue());
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserRefFitHeightPropagationTest,
	"Design2UMG.Unit.PenParser.RefFitHeightPropagation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserRefFitHeightPropagationTest::RunTest(const FString&)
{
	const FString RefTableJson =
		TEXT("{")
		TEXT("\"version\":\"1.0\",")
		TEXT("\"children\":[")
		TEXT("{\"id\":\"cell-template\",\"name\":\"CellTemplate\",\"type\":\"frame\",\"reusable\":true,\"width\":1440,\"height\":\"fit_content(56)\"},")
		TEXT("{\"id\":\"row-template\",\"name\":\"RowTemplate\",\"type\":\"frame\",\"reusable\":true,\"width\":1440,\"height\":\"fit_content(44)\",\"layout\":\"horizontal\",\"children\":[")
		TEXT("{\"id\":\"cell\",\"name\":\"Cell\",\"type\":\"ref\",\"ref\":\"cell-template\"}")
		TEXT("]},")
		TEXT("{\"id\":\"table-template\",\"name\":\"TableTemplate\",\"type\":\"frame\",\"reusable\":true,\"width\":1440,\"height\":\"fit_content\",\"layout\":\"vertical\",\"children\":[")
		TEXT("{\"id\":\"header-row\",\"name\":\"HeaderRow\",\"type\":\"frame\",\"width\":1440,\"height\":\"fit_content(44)\"},")
		TEXT("{\"id\":\"row-1\",\"name\":\"Row1\",\"type\":\"ref\",\"ref\":\"row-template\",\"children\":[{\"id\":\"cell-1\",\"name\":\"Cell1\",\"type\":\"ref\",\"ref\":\"cell-template\"}]},")
		TEXT("{\"id\":\"row-2\",\"name\":\"Row2\",\"type\":\"ref\",\"ref\":\"row-template\",\"children\":[{\"id\":\"cell-2\",\"name\":\"Cell2\",\"type\":\"ref\",\"ref\":\"cell-template\"}]},")
		TEXT("{\"id\":\"row-3\",\"name\":\"Row3\",\"type\":\"ref\",\"ref\":\"row-template\",\"children\":[{\"id\":\"cell-3\",\"name\":\"Cell3\",\"type\":\"ref\",\"ref\":\"cell-template\"}]},")
		TEXT("{\"id\":\"row-4\",\"name\":\"Row4\",\"type\":\"ref\",\"ref\":\"row-template\",\"children\":[{\"id\":\"cell-4\",\"name\":\"Cell4\",\"type\":\"ref\",\"ref\":\"cell-template\"}]}")
		TEXT("]},")
		TEXT("{\"id\":\"root\",\"name\":\"Root\",\"type\":\"frame\",\"width\":1440,\"height\":\"fit_content\",\"layout\":\"vertical\",\"children\":[")
		TEXT("{\"id\":\"table-instance\",\"name\":\"TableInstance\",\"type\":\"ref\",\"ref\":\"table-template\",\"height\":\"fit_content\"}")
		TEXT("]}")
		TEXT("]}")
		;

	FDesignDocument Doc;
	if (!TestTrue(TEXT("Ref table document parses"), FPenParser::ParseDocument(RefTableJson, Doc)))
	{
		return false;
	}
	TestEqual(TEXT("Ref table roots parsed"), Doc.Children.Num(), 4);
	if (Doc.Children.Num() != 4 || !Doc.Children[3].IsValid() || Doc.Children[3]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode> Root = Doc.Children[3];
	const TSharedPtr<FDesignNode> TableRef = Root->Children[0];
	const FVector2D Precomputed = SizeEstimator::PromoteFitContentSizes(Root, Doc,
		[&Doc](const TSharedPtr<FDesignNode>& RefNode) { return RefExpander::ExpandRef(RefNode, Doc); });
	TestEqual(TEXT("Precompute sees the expanded table height"), Precomputed.Y, 268.0);
	TestEqual(TEXT("Ref instance keeps the expanded table height"), TableRef->Transform.Height.Value, 268.0f);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		TSharedPtr<FDesignNode> LocalRowRef = MakeShared<FDesignNode>();
		LocalRowRef->Id = FString::Printf(TEXT("local-row-%d"), Index);
		LocalRowRef->Name = LocalRowRef->Id;
		LocalRowRef->Type = EDesignNodeType::Ref;
		LocalRowRef->Ref = TEXT("row-template");
		LocalRowRef->Transform.Width = FDesignSize::Fixed(1440.0f);
		LocalRowRef->Transform.Height = FDesignSize::Fit(56.0f);
		LocalRowRef->Transform.bHasWidth = true;
		LocalRowRef->Transform.bHasHeight = true;
		LocalRowRef->Transform.bSourceHasWidth = true;
		LocalRowRef->Transform.bSourceHasHeight = true;

		TSharedPtr<FDesignNode> LocalCellRef = MakeShared<FDesignNode>();
		LocalCellRef->Id = FString::Printf(TEXT("local-cell-%d"), Index);
		LocalCellRef->Name = LocalCellRef->Id;
		LocalCellRef->Type = EDesignNodeType::Ref;
		LocalCellRef->Ref = TEXT("cell-template");
		LocalRowRef->Children.Add(LocalCellRef);

		TableRef->Children.Add(LocalRowRef);
	}

	const FVector2D ParentEstimate = SizeEstimator::EstimateNodeSize(Root, 1440.0f);
	TestEqual(TEXT("Parent uses expanded ref table height instead of one row"), ParentEstimate.Y, 268.0);
	TestTrue(TEXT("Row-like source fit minimum remains source-authored"),
		TableRef->Children[1]->Transform.bSourceHasHeight);

	return true;
}

// ---------------------------------------------------------------------------
// Size DSL: fill_container / fit_content / numeric strings / zero edge cases
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserSizeDSLTest,
	"Design2UMG.Unit.PenParser.SizeDSL",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserSizeDSLTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("size_dsl.json"), Doc))
		return false;

	TestEqual(TEXT("Eight root children"), Doc.Children.Num(), 8);

	// fill_container without argument -> Fill(0)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[0];
		TestTrue(TEXT("fill_container node valid"), N.IsValid());
		ExpectSize(*this, TEXT("fill_container width"), N->Transform.Width, EDesignSizeMode::FillContainer, 0.0f);
		ExpectSize(*this, TEXT("fill_container height"), N->Transform.Height, EDesignSizeMode::FillContainer, 0.0f);
	}

	// fill_container(N) -> Fill(N)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[1];
		TestTrue(TEXT("fill_container(N) node valid"), N.IsValid());
		ExpectSize(*this, TEXT("fill_container(200) width"), N->Transform.Width, EDesignSizeMode::FillContainer, 200.0f);
		ExpectSize(*this, TEXT("fill_container(100) height"), N->Transform.Height, EDesignSizeMode::FillContainer, 100.0f);
	}

	// fit_content without argument -> Fit(0)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[2];
		TestTrue(TEXT("fit_content node valid"), N.IsValid());
		ExpectSize(*this, TEXT("fit_content width"), N->Transform.Width, EDesignSizeMode::FitContent, 0.0f);
		ExpectSize(*this, TEXT("fit_content height"), N->Transform.Height, EDesignSizeMode::FitContent, 0.0f);
	}

	// fit_content(N) -> Fit(N)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[3];
		TestTrue(TEXT("fit_content(N) node valid"), N.IsValid());
		ExpectSize(*this, TEXT("fit_content(50) width"), N->Transform.Width, EDesignSizeMode::FitContent, 50.0f);
		ExpectSize(*this, TEXT("fit_content(80) height"), N->Transform.Height, EDesignSizeMode::FitContent, 80.0f);
	}

	// Numeric string -> Fixed(N)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[4];
		TestTrue(TEXT("numeric string node valid"), N.IsValid());
		ExpectSize(*this, TEXT("numeric string width"), N->Transform.Width, EDesignSizeMode::Fixed, 128.0f);
		ExpectSize(*this, TEXT("numeric string height"), N->Transform.Height, EDesignSizeMode::Fixed, 64.0f);
	}

	// Variable references resolve through the number path.
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[5];
		TestTrue(TEXT("variable-reference node valid"), N.IsValid());
		ExpectSize(*this, TEXT("variable reference width"), N->Transform.Width, EDesignSizeMode::Fixed, 96.0f);
		ExpectSize(*this, TEXT("variable reference zero height"), N->Transform.Height, EDesignSizeMode::FitContent, 0.0f);
	}

	// Zero numeric on non-line node -> Fit()
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[6];
		TestTrue(TEXT("zero-numeric node valid"), N.IsValid());
		ExpectSize(*this, TEXT("zero on non-line width"), N->Transform.Width, EDesignSizeMode::FitContent, 0.0f);
		ExpectSize(*this, TEXT("zero on non-line height"), N->Transform.Height, EDesignSizeMode::FitContent, 0.0f);
	}

	// Zero numeric on line node -> Fixed(0)
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[7];
		TestTrue(TEXT("line-zero-numeric node valid"), N.IsValid());
		ExpectSize(*this, TEXT("zero on line width"), N->Transform.Width, EDesignSizeMode::Fixed, 0.0f);
		ExpectSize(*this, TEXT("zero on line height"), N->Transform.Height, EDesignSizeMode::Fixed, 0.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Malformed size DSL inputs - parser must not crash, must degrade gracefully
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserSizeDSLErrorTest,
	"Design2UMG.Unit.PenParser.SizeDSLError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserSizeDSLErrorTest::RunTest(const FString&)
{
	// Malformed: no closing paren -> treat as bare keyword, default fallback.
	{
		const FString Json =
			TEXT("{")
			TEXT("\"version\":\"1.0\",")
			TEXT("\"children\":[")
			TEXT("{\"id\":\"n1\",\"name\":\"N1\",\"type\":\"frame\",\"x\":0,\"y\":0,")
			TEXT("\"width\":\"fill_container(\",\"height\":\"fit_content(\"}")
			TEXT("]}");
		FDesignDocument Doc;
		TestTrue(TEXT("Malformed fill_container( parses"), FPenParser::ParseDocument(Json, Doc));
		TestEqual(TEXT("Malformed: one child"), Doc.Children.Num(), 1);
		ExpectKeywordFallbackSize(*this, TEXT("fill_container( fallback width"), Doc.Children[0]->Transform.Width, EDesignSizeMode::FillContainer);
		ExpectKeywordFallbackSize(*this, TEXT("fit_content( fallback height"), Doc.Children[0]->Transform.Height, EDesignSizeMode::FitContent);
	}

	// Empty parens -> empty string -> not numeric -> zero fallback.
	{
		const FString Json =
			TEXT("{")
			TEXT("\"version\":\"1.0\",")
			TEXT("\"children\":[")
			TEXT("{\"id\":\"n2\",\"name\":\"N2\",\"type\":\"frame\",\"x\":0,\"y\":0,")
			TEXT("\"width\":\"fill_container()\",\"height\":\"fit_content()\"}")
			TEXT("]}");
		FDesignDocument Doc;
		TestTrue(TEXT("Empty parens parses"), FPenParser::ParseDocument(Json, Doc));
		TestEqual(TEXT("Empty parens: one child"), Doc.Children.Num(), 1);
		ExpectKeywordFallbackSize(*this, TEXT("fill_container() width"), Doc.Children[0]->Transform.Width, EDesignSizeMode::FillContainer);
		ExpectKeywordFallbackSize(*this, TEXT("fit_content() height"), Doc.Children[0]->Transform.Height, EDesignSizeMode::FitContent);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Gradient fills: CSS function strings and Pencil rotation convention
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserGradientFillsTest,
	"Design2UMG.Unit.PenParser.GradientFills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserGradientFillsTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("gradient_fills.json"), Doc))
		return false;

	TestEqual(TEXT("Two root children"), Doc.Children.Num(), 2);

	// CSS gradient string -> typed gradient with endpoints, not a solid color.
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[0];
		TestTrue(TEXT("css gradient node valid"), N.IsValid());
		const FDesignFill& Fill = N->Visual.TopmostFill();
		TestEqual(TEXT("css gradient fill type"), Fill.Type, EDesignFillType::Gradient);
		TestTrue(TEXT("css gradient enabled"), Fill.bEnabled);
		TestEqual(TEXT("css gradient stop count"), Fill.Gradient.Colors.Num(), 3);
		TestEqual(TEXT("css gradient stop0"), Fill.Gradient.Colors[0].Color, FString(TEXT("#0F0F0F60")));
		TestEqual(TEXT("css gradient stop0 position"), Fill.Gradient.Colors[0].Position, 0.0f);
		TestEqual(TEXT("css gradient stop1 position"), Fill.Gradient.Colors[1].Position, 0.35f);
		TestEqual(TEXT("css gradient stop2 position"), Fill.Gradient.Colors[2].Position, 1.0f);
		TestTrue(TEXT("css gradient has start"), Fill.Gradient.StartPoint.IsSet());
		TestTrue(TEXT("css gradient has end"), Fill.Gradient.EndPoint.IsSet());
		if (Fill.Gradient.StartPoint.IsSet() && Fill.Gradient.EndPoint.IsSet())
		{
			TestEqual(TEXT("css gradient start x"), static_cast<float>(Fill.Gradient.StartPoint->X), 0.5f);
			TestEqual(TEXT("css gradient start y"), static_cast<float>(Fill.Gradient.StartPoint->Y), 0.0f);
			TestEqual(TEXT("css gradient end x"), static_cast<float>(Fill.Gradient.EndPoint->X), 0.5f);
			TestEqual(TEXT("css gradient end y"), static_cast<float>(Fill.Gradient.EndPoint->Y), 1.0f);
		}
	}

	// Structured linear gradient: Pencil rotation 225 maps to the y-down math
	// angle -225-90 = -315, i.e. growth direction (cos, sin) = (+0.707, +0.707)
	// toward the bottom-right with stop 0 in the top-left.
	{
		const TSharedPtr<FDesignNode>& N = Doc.Children[1];
		TestTrue(TEXT("structured gradient node valid"), N.IsValid());
		const FDesignFill& Fill = N->Visual.TopmostFill();
		TestEqual(TEXT("structured gradient fill type"), Fill.Type, EDesignFillType::Gradient);
		TestEqual(TEXT("structured gradient stop count"), Fill.Gradient.Colors.Num(), 2);
		TestEqual(TEXT("pencil rotation 225 -> -315"), Fill.Gradient.Rotation, -315.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Imported library variables: "$Y:--token" refs through the imports map with
// node-level theme overrides (values verified against Pencil-rendered
// references). The library table is registered from a fixture the same way the
// editor-side loader registers real extension libraries.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserImportedVariablesTest,
	"Design2UMG.Unit.PenParser.ImportedVariables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserImportedVariablesTest::RunTest(const FString&)
{
	// The unknown-alias case intentionally logs one unresolved-variable warning.
	AddExpectedError(TEXT("Variable not found: Z:--primary"), EAutomationExpectedErrorFlags::Contains, 1);

	// Library variables come from the runtime registry (normally filled from the
	// Pencil extension's *.lib.pen files); register the fixture library that the
	// document's imports map references before parsing.
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin.IsValid())
		{
			AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString LibraryPath = FPaths::Combine(Plugin->GetBaseDir(), FixtureBase, TEXT("library_variables.json"));
		FString LibraryVariablesJson;
		if (!FFileHelper::LoadFileToString(LibraryVariablesJson, *LibraryPath))
		{
			AddError(FString::Printf(TEXT("Failed to load fixture: %s"), *LibraryPath));
			return false;
		}
		PenBundledLibraries::RegisterLibraryVariables(TEXT("sample"), LibraryVariablesJson);
	}

	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("imported_variables.json"), Doc))
		return false;

	TestTrue(TEXT("Imported variable is namespaced under its alias"), Doc.Variables.Contains(TEXT("Y:--primary")));

	// Alias-qualified accent override selects the themed library value.
	{
		const TSharedPtr<FDesignNode>& Page = Doc.Children[0];
		TestEqual(TEXT("orange dot fill"), Page->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#EA580C")));
		// --muted-foreground has no accent variants, so the default applies.
		TestEqual(TEXT("muted text fill"), Page->Children[1]->Visual.TopmostFill().SolidColor, FString(TEXT("#737373")));
	}

	// Unprefixed axes apply to every library; the most specific matching entry wins.
	{
		const TSharedPtr<FDesignNode>& Page = Doc.Children[1];
		TestEqual(TEXT("dark+gray background fill"), Page->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#030712")));
		// --primary has a dedicated {Mode:Dark,Base:Gray} entry, which outranks {Mode:Dark}.
		TestEqual(TEXT("dark+gray primary fill"), Page->Children[1]->Visual.TopmostFill().SolidColor, FString(TEXT("#E5E7EB")));
	}

	// Document-declared plain refs keep resolving; unknown aliases pass through
	// unchanged instead of fabricating a color.
	{
		const TSharedPtr<FDesignNode>& Page = Doc.Children[2];
		TestEqual(TEXT("document variable fill"), Page->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#123456")));
		TestEqual(TEXT("unknown alias fill stays raw"), Page->Children[1]->Visual.TopmostFill().SolidColor, FString(TEXT("$Z:--primary")));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Sticky nodes: prompt/note synthesize the Pencil sticky-card subtree
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserStickyNodesTest,
	"Design2UMG.Unit.PenParser.StickyNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserStickyNodesTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadPenFixture(*this, TEXT("sticky_nodes.json"), Doc))
		return false;

	const TSharedPtr<FDesignNode>& Wrap = Doc.Children[0];
	TestEqual(TEXT("Wrap has 3 sticky children"), Wrap->Children.Num(), 3);

	const TSharedPtr<FDesignNode>& Prompt = Wrap->Children[0];
	TestEqual(TEXT("Prompt becomes a card frame"), Prompt->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Prompt card layout"), Prompt->LayoutData.Layout, EDesignLayout::Vertical);
	TestEqual(TEXT("Prompt card fill"), Prompt->Visual.TopmostFill().SolidColor, FString(TEXT("#E8F6FF")));
	TestEqual(TEXT("Prompt card outline"), Prompt->Visual.Stroke.Fill->SolidColor, FString(TEXT("#009DFF")));
	TestEqual(TEXT("Prompt card corner radius"), Prompt->Visual.CornerRadius.Uniform, 8.0f);
	// Declared height is editor chrome state; the card always fits its content.
	ExpectKeywordFallbackSize(*this, TEXT("Prompt card height"), Prompt->Transform.Height, EDesignSizeMode::FitContent);
	TestTrue(TEXT("Prompt card keeps fill width"), Prompt->Transform.Width.IsFill());
	TestEqual(TEXT("Prompt card has header/body/footer"), Prompt->Children.Num(), 3);

	const TSharedPtr<FDesignNode>& Header = Prompt->Children[0];
	TestEqual(TEXT("Header height"), Header->Transform.Height.Mode, EDesignSizeMode::Fixed);
	TestEqual(TEXT("Header height value"), Header->Transform.Height.Value, 45.0f);
	TestEqual(TEXT("Header fill"), Header->Visual.TopmostFill().SolidColor, FString(TEXT("#C3E8FF")));
	TestEqual(TEXT("Header dash pattern"), Header->Visual.Stroke.DashPattern.Num(), 2);
	const TSharedPtr<FDesignNode>& HeaderLabel = Header->Children[0];
	TestEqual(TEXT("Header label text"), HeaderLabel->Text.Content, FString(TEXT("Prompt")));
	TestEqual(TEXT("Header label color"), HeaderLabel->Visual.TopmostFill().SolidColor, FString(TEXT("#006CAF")));

	const TSharedPtr<FDesignNode>& Body = Prompt->Children[1];
	const TSharedPtr<FDesignNode>& ContentText = Body->Children[0];
	TestEqual(TEXT("Sticky content preserved"), ContentText->Text.Content, FString(TEXT("Line one of the prompt.\nLine two of the prompt.")));
	TestEqual(TEXT("Content text color"), ContentText->Visual.TopmostFill().SolidColor, FString(TEXT("#006CAF")));
	TestEqual(TEXT("Content text growth"), ContentText->Text.TextGrowth, EDesignTextGrowth::FixedWidth);
	TestEqual(TEXT("Content min height"), ContentText->Transform.Height.Value, 116.0f);

	const TSharedPtr<FDesignNode>& Footer = Prompt->Children[2];
	TestEqual(TEXT("Footer has spacer and copy button"), Footer->Children.Num(), 2);
	const TSharedPtr<FDesignNode>& CopyButton = Footer->Children[1];
	TestEqual(TEXT("Copy button fill"), CopyButton->Visual.TopmostFill().SolidColor, FString(TEXT("#006CAF")));
	TestEqual(TEXT("Copy label text"), CopyButton->Children[0]->Text.Content, FString(TEXT("Copy")));
	TestEqual(TEXT("Copy label color"), CopyButton->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#FFFFFF")));

	const TSharedPtr<FDesignNode>& Note = Wrap->Children[1];
	TestEqual(TEXT("Note becomes a card frame"), Note->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Note card fill"), Note->Visual.TopmostFill().SolidColor, FString(TEXT("#FFF7E5")));
	// Fixed widths below the sticky minimum are clamped up to it.
	TestEqual(TEXT("Note card width mode"), Note->Transform.Width.Mode, EDesignSizeMode::Fixed);
	TestEqual(TEXT("Note card clamped width"), Note->Transform.Width.Value, 250.0f);
	TestEqual(TEXT("Note card has header/body only"), Note->Children.Num(), 2);
	TestEqual(TEXT("Note content min height"), Note->Children[1]->Children[0]->Transform.Height.Value, 150.0f);

	const TSharedPtr<FDesignNode>& Context = Wrap->Children[2];
	TestEqual(TEXT("Context becomes a card frame"), Context->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Context card fill"), Context->Visual.TopmostFill().SolidColor, FString(TEXT("#F0F0F0")));
	TestEqual(TEXT("Context card outline"), Context->Visual.Stroke.Fill->SolidColor, FString(TEXT("#767676")));
	TestEqual(TEXT("Context header fill"), Context->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#FFFFFF")));
	TestEqual(TEXT("Context header label"), Context->Children[0]->Children[0]->Text.Content, FString(TEXT("Context")));
	TestEqual(TEXT("Context label color"), Context->Children[0]->Children[0]->Visual.TopmostFill().SolidColor, FString(TEXT("#2C2C2C")));
	TestTrue(TEXT("Context card keeps fill width"), Context->Transform.Width.IsFill());
	TestEqual(TEXT("Context content min height"), Context->Children[1]->Children[0]->Transform.Height.Value, 150.0f);
	TestEqual(TEXT("Context body text preserved"), Context->Children[1]->Children[0]->Text.Content, FString(TEXT("Context body text.")));

	return true;
}

// ---------------------------------------------------------------------------
// Unresolvable paint literals: Pencil drops them instead of painting
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserUnresolvablePaintTest,
	"Design2UMG.Unit.PenParser.UnresolvablePaint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserUnresolvablePaintTest::RunTest(const FString&)
{
	// Unresolvable string paints are skipped. Fill arrays continue to a valid entry;
	// empty and numeric strings are dropped, while valid object colors are retained.
	const FString Json =
		TEXT("{")
		TEXT("\"version\":\"1.0\",")
		TEXT("\"variables\":{\"destructive\":{\"type\":\"color\",\"value\":\"#DC2626\"}},")
		TEXT("\"children\":[")
		TEXT("{\"id\":\"escaped\",\"name\":\"Escaped\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"\\\\$destructive\"},")
		TEXT("{\"id\":\"hexnode\",\"name\":\"HexNode\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"#AABBCC\"},")
		TEXT("{\"id\":\"namednode\",\"name\":\"NamedNode\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"RED\"},")
		TEXT("{\"id\":\"varnode\",\"name\":\"VarNode\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"$destructive\"},")
		TEXT("{\"id\":\"malformedhex\",\"name\":\"MalformedHex\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"#xyz\"},")
		TEXT("{\"id\":\"arrayfill\",\"name\":\"ArrayFill\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":[\"#112233\",\"\\\\$destructive\"]},")
		TEXT("{\"id\":\"emptyfill\",\"name\":\"EmptyFill\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"\"},")
		TEXT("{\"id\":\"numericfill\",\"name\":\"NumericFill\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":\"123\"},")
		TEXT("{\"id\":\"objfill\",\"name\":\"ObjFill\",\"type\":\"frame\",\"width\":100,\"height\":40,")
		TEXT("\"fill\":{\"type\":\"color\",\"enabled\":true,\"color\":\"#ABCDEF\"}}")
		TEXT("]}");
	FDesignDocument Doc;
	TestTrue(TEXT("Unresolvable paint document parses"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Nine root children"), Doc.Children.Num(), 9);

	// Escaped literal "\$destructive" is plain text, not a variable ref, and no
	// color literal: Pencil renders the node with no fill (stays transparent).
	{
		const FDesignFill& Fill = Doc.Children[0]->Visual.TopmostFill();
		TestEqual(TEXT("escaped literal fill dropped"), Fill.Type, EDesignFillType::None);
		TestTrue(TEXT("escaped literal has no source var"), Fill.SourceVar.IsEmpty());
	}

	// Hex and named literals keep producing a solid color fill.
	TestEqual(TEXT("hex fill type"), Doc.Children[1]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("hex fill color"), Doc.Children[1]->Visual.TopmostFill().SolidColor, FString(TEXT("#AABBCC")));
	TestEqual(TEXT("named fill type"), Doc.Children[2]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("named fill color"), Doc.Children[2]->Visual.TopmostFill().SolidColor, FString(TEXT("RED")));

	// A real variable ref still resolves through the registry.
	TestEqual(TEXT("variable fill type"), Doc.Children[3]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("variable fill color"), Doc.Children[3]->Visual.TopmostFill().SolidColor, FString(TEXT("#DC2626")));

	// Hex-prefixed values pass through to the renderer, including malformed ones.
	TestEqual(TEXT("malformed hex fill type"), Doc.Children[4]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("malformed hex stays untouched"), Doc.Children[4]->Visual.TopmostFill().SolidColor, FString(TEXT("#xyz")));

	// An escaped entry inside a fill array is dropped without aborting the
	// scan; the next entry down (array scans from the tail) still wins.
	TestEqual(TEXT("array fill type"), Doc.Children[5]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("array fill falls back to the legal entry"), Doc.Children[5]->Visual.TopmostFill().SolidColor, FString(TEXT("#112233")));

	// The empty string and a plain numeric string are not paint values; Pencil
	// leaves the node unfilled instead of painting them.
	TestEqual(TEXT("empty string fill dropped"), Doc.Children[6]->Visual.TopmostFill().Type, EDesignFillType::None);
	TestEqual(TEXT("numeric string fill dropped"), Doc.Children[7]->Visual.TopmostFill().Type, EDesignFillType::None);

	// Valid object-form colors are unaffected by string-paint filtering.
	TestEqual(TEXT("object fill type"), Doc.Children[8]->Visual.TopmostFill().Type, EDesignFillType::Color);
	TestEqual(TEXT("object fill color"), Doc.Children[8]->Visual.TopmostFill().SolidColor, FString(TEXT("#ABCDEF")));

	return true;
}

// ---------------------------------------------------------------------------
// shared tree budget. A wide-but-shallow document (many siblings, shallow
// depth) must be bounded by the single node-count budget just like a deep tree,
// and a breach must reject the WHOLE parse (never a truncated success tree).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserNodeCountBudgetTest,
	"Design2UMG.Unit.PenParser.NodeCountBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenParserNodeCountBudgetTest::RunTest(const FString&)
{
	// The breach logs once at the offending node and once at the document level.
	AddExpectedError(TEXT("exceeds the maximum parsed node count"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("exceeds supported size/depth limits"), EAutomationExpectedErrorFlags::Contains, 1);

	// Build a wide, shallow document: one root with (MaxParsedNodeCount + 1) minimal
	// children. Depth stays at 1, so this exercises node-count, not depth.
	TArray<FString> Children;
	Children.Reserve(DesignTreeLimits::MaxParsedNodeCount + 1);
	for (int32 Index = 0; Index < DesignTreeLimits::MaxParsedNodeCount + 1; ++Index)
	{
		Children.Add(FString::Printf(TEXT("{\"id\":\"n%d\",\"name\":\"n%d\",\"type\":\"frame\"}"), Index, Index));
	}
	const FString ChildrenJson = FString::Join(Children, TEXT(","));
	const FString Json = FString::Printf(
		TEXT("{\"version\":\"1.0\",\"children\":[%s]}"), *ChildrenJson);

	FDesignDocument Doc;
	TestFalse(TEXT("Wide-but-shallow document over the node-count budget is rejected"), FPenParser::ParseDocument(Json, Doc));
	// No truncated tree is published: a rejected parse yields no children.
	TestEqual(TEXT("Rejected parse publishes no nodes"), Doc.Children.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSizeEstimatorFlowFallbackTest,
	"Design2UMG.Unit.SizeEstimator.FlowFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSizeEstimatorFlowFallbackTest::RunTest(const FString&)
{
	for (const bool bVertical : {false, true})
	{
		FDesignDocument Document;
		const FString Json = FString::Printf(TEXT(R"JSON({"version":"2.14","children":[
		{"id":"flow","type":"frame","layout":"%s","width":"fit_content(120)","height":"fit_content(120)",
		"padding":6,"gap":8,"children":[
		{"id":"a","type":"rectangle","width":20,"height":20,"fill":"#ff0000"},
		{"id":"b","type":"rectangle","width":20,"height":20,"fill":"#ff0000"}]}]})JSON"), bVertical ? TEXT("vertical") : TEXT("horizontal"));
		if (!TestTrue(TEXT("Flow fallback fixture parses"), FPenParser::ParseDocument(Json, Document))) return false;
		const auto Node = Document.Children[0];
		const FVector2D Expected = bVertical ? FVector2D(32, 60) : FVector2D(60, 32);
		TestEqual(TEXT("Read-only estimate uses children rather than fallback minimum"), SizeEstimator::EstimateNodeSize(Node), Expected);
		auto Resolve = [&Document](const TSharedPtr<FDesignNode>& Ref) { return RefExpander::ExpandRef(Ref, Document); };
		TestEqual(TEXT("Promotion stores actual flow content dimensions"), SizeEstimator::PromoteFitContentSizes(Node, Document, Resolve), Expected);
		Node->Children.RemoveAt(1);
		TestEqual(TEXT("A subsequent smaller content measurement can shrink"), SizeEstimator::PromoteFitContentSizes(Node, Document, Resolve), FVector2D(32, 32));
		Node->Transform.Width = FDesignSize::Fixed(120);
		TestEqual(TEXT("Fixed size stays fixed"), SizeEstimator::PromoteFitContentSizes(Node, Document, Resolve).X, 120.0);
		Node->Transform.Width = FDesignSize::Fill(120);
		TestEqual(TEXT("Fill size is not replaced by content"), SizeEstimator::PromoteFitContentSizes(Node, Document, Resolve).X, 120.0);
		Node->Transform.Height = FDesignSize::Fit(120);
		Node->Children[0]->Transform.bEnabled = false;
		TestEqual(TEXT("No visible content keeps the declared fallback"), SizeEstimator::PromoteFitContentSizes(Node, Document, Resolve).Y, 120.0);
	}
	FDesignDocument Document;
	if (!TestTrue(TEXT("Slot fallback fixture parses"), FPenParser::ParseDocument(TEXT(R"JSON({"version":"2.14","children":[
		{"id":"slot","type":"frame","reusable":true,"layout":"vertical","width":80,"height":"fit_content(120)","padding":6},
		{"id":"instance","type":"ref","ref":"slot","height":"fit_content(120)","children":[
		{"id":"row","type":"rectangle","width":20,"height":20,"fill":"#ff0000"}]}]})JSON"), Document))) return false;
	auto Resolve = [&Document](const TSharedPtr<FDesignNode>& Ref) { return RefExpander::ExpandRef(Ref, Document); };
	TestEqual(TEXT("Empty slot retains its fallback height"), SizeEstimator::PromoteFitContentSizes(Document.Children[0], Document, Resolve).Y, 120.0);
	TestEqual(TEXT("Populated ref measures its replacement children"), SizeEstimator::PromoteFitContentSizes(Document.Children[1], Document, Resolve).Y, 32.0);
	TestEqual(TEXT("Instance cached height is updated when content shrinks"), Document.Children[1]->Transform.Height.Value, 32.0f);
	TestTrue(TEXT("A measured ref preserves the definition's fixed width mode"), Document.Children[1]->Transform.Width.IsFixed());
	TestEqual(TEXT("Inherited fixed width is not replaced by narrower content"), Document.Children[1]->Transform.Width.Value, 80.f);
	TestTrue(TEXT("An explicitly authored fit height stays content-sized"), Document.Children[1]->Transform.Height.IsFit());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenPlaceholderRequiresSemanticFieldTest,
	"Design2UMG.Unit.PenParser.PlaceholderRequiresSemanticField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenPlaceholderRequiresSemanticFieldTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Semantic surface fixture parses"), FPenParser::ParseDocument(TEXT(R"JSON({"version":"2.14","children":[
		{"id":"art","type":"frame","name":"image-placeholder","width":160,"height":80,"fill":"#eeeeee"},
		{"id":"named","type":"frame","name":"slot content","width":160,"height":80},
		{"id":"slot","type":"frame","slot":[],"width":160,"height":80},
		{"id":"marked","type":"frame","placeholder":true,"width":160,"height":80}
	]})JSON"), Document))) return false;
	TestFalse(TEXT("An artwork name does not override its authored paint"), Document.Children[0]->LayoutData.bSemanticPlaceholderSurface);
	TestFalse(TEXT("A slot-like name alone has no semantic meaning"), Document.Children[1]->LayoutData.bSemanticPlaceholderSurface);
	TestTrue(TEXT("An explicit empty slot remains a semantic surface"), Document.Children[2]->LayoutData.bSemanticPlaceholderSurface);
	TestTrue(TEXT("An explicit placeholder marker remains supported"), Document.Children[3]->LayoutData.bSemanticPlaceholderSurface);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
