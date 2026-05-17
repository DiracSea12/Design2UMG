// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "RefExpander.h"
#include "DesignSchema.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
