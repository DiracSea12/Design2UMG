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

#endif // WITH_DEV_AUTOMATION_TESTS
