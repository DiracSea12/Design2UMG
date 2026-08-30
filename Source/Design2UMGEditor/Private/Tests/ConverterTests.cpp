// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "PenParser.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGTestNodes.h"
#include "Design2UMGTestUtils.h"

namespace
{
	using Design2UMGEditorTests::MakeNode;
	using Design2UMGTestUtils::FindWidgetById;

	int32 CountWidgetsById(const FUMGWidgetNode& Root, const FString& Id)
	{
		int32 Count = Root.Id == Id ? 1 : 0;
		for (const FUMGWidgetNode& Child : Root.Children)
		{
			Count += CountWidgetsById(Child, Id);
		}
		return Count;
	}

	FDesignDocument BuildSingleNodeDocument()
	{
		FDesignDocument Doc;
		Doc.Version = TEXT("converter-single");
		Doc.Children.Add(MakeNode(TEXT("single-1"), TEXT("SingleNode"), EDesignNodeType::Frame, 0.0f, 0.0f, 200.0f, 100.0f));
		return Doc;
	}

	FDesignDocument BuildNestedNodesDocument()
	{
		FDesignDocument Doc;
		Doc.Version = TEXT("converter-nested");

		const TSharedPtr<FDesignNode> Parent =
			MakeNode(TEXT("parent-1"), TEXT("ParentFrame"), EDesignNodeType::Frame, 0.0f, 0.0f, 400.0f, 300.0f);
		const TSharedPtr<FDesignNode> Rect =
			MakeNode(TEXT("child-1"), TEXT("ChildRect"), EDesignNodeType::Rectangle, 10.0f, 10.0f, 100.0f, 50.0f);
		Rect->Visual.Fills.Add(FDesignFill::FromColor(TEXT("#ff0000")));

		const TSharedPtr<FDesignNode> Text =
			MakeNode(TEXT("child-2"), TEXT("ChildText"), EDesignNodeType::Text, 10.0f, 70.0f, 200.0f, 30.0f);
		Text->Text.Content = TEXT("Nested text");
		Text->Text.FontFamily = TEXT("Roboto");
		Text->Text.FontSize = 14.0f;

		Parent->Children.Add(Rect);
		Parent->Children.Add(Text);
		Doc.Children.Add(Parent);
		return Doc;
	}

	FDesignDocument BuildRefExpandDocument()
	{
		FDesignDocument Doc;
		Doc.Version = TEXT("converter-ref");

		const TSharedPtr<FDesignNode> Reusable =
			MakeNode(TEXT("reusable-1"), TEXT("ReusableComp"), EDesignNodeType::Frame, 0.0f, 0.0f, 100.0f, 100.0f);
		Reusable->bReusable = true;
		Reusable->Children.Add(
			MakeNode(TEXT("rc-child-1"), TEXT("RCChild"), EDesignNodeType::Rectangle, 0.0f, 0.0f, 100.0f, 100.0f));

		const TSharedPtr<FDesignNode> Host =
			MakeNode(TEXT("host-1"), TEXT("HostFrame"), EDesignNodeType::Frame, 200.0f, 0.0f, 400.0f, 300.0f);
		const TSharedPtr<FDesignNode> Ref =
			MakeNode(TEXT("ref-1"), TEXT("RefNode"), EDesignNodeType::Ref, 10.0f, 10.0f, 100.0f, 100.0f);
		Ref->Ref = TEXT("reusable-1");
		Host->Children.Add(Ref);

		Doc.Children.Add(Reusable);
		Doc.Children.Add(Host);
		return Doc;
	}

	bool BuildPenAuthoredTextBoxOwnerDocument(FDesignDocument& OutDoc)
	{
		const FString Json = TEXT(R"JSON(
{
  "version": "1.0",
  "children": [
    {
      "id": "root",
      "name": "Root",
      "type": "frame",
      "width": 400,
      "height": 120,
      "layout": "vertical",
      "padding": 20,
      "children": [
        {
          "id": "estimated-text",
          "name": "Estimated Text",
          "type": "text",
          "content": "abcd",
          "fontSize": 20,
          "lineHeight": 1.4,
          "width": 44.8,
          "height": 28
        }
      ]
    }
  ]
}
)JSON");
		return FPenParser::ParseDocument(Json, OutDoc);
	}

}

// ---------------------------------------------------------------------------
// Conversion recursion guard behavior
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterDeepTreeDoesNotCrashTest,
	"Design2UMG.Unit.Converter.DeepTreeDoesNotCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterDeepTreeDoesNotCrashTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	Doc.Version = TEXT("converter-deep-tree");

	const TSharedPtr<FDesignNode> Root =
		MakeNode(TEXT("deep-root"), TEXT("DeepRoot"), EDesignNodeType::Frame, 0.0f, 0.0f, 100.0f, 100.0f);
	TSharedPtr<FDesignNode> Current = Root;
	constexpr int32 ExpectedDepth = 64;
	for (int32 Index = 0; Index < ExpectedDepth; ++Index)
	{
		const TSharedPtr<FDesignNode> Child = MakeNode(
			FString::Printf(TEXT("deep-%d"), Index),
			FString::Printf(TEXT("Deep%d"), Index),
			EDesignNodeType::Frame,
			0.0f,
			0.0f,
			100.0f,
			100.0f);
		Current->Children.Add(Child);
		Current = Child;
	}
	Doc.Children.Add(Root);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Root widget name"), Data.RootWidget.Name, FString(TEXT("DeepRoot")));
	const FUMGWidgetNode* CurrentWidget = &Data.RootWidget;
	int32 ConvertedDepth = 0;
	while (CurrentWidget && CurrentWidget->Children.Num() > 0)
	{
		CurrentWidget = &CurrentWidget->Children[0];
		++ConvertedDepth;
	}
	TestTrue(TEXT("Deep conversion completes for a nested tree below the recursion guard limit"), ConvertedDepth >= ExpectedDepth);

	return true;
}

// ---------------------------------------------------------------------------
// Convert single node
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterSingleNodeTest,
	"Design2UMG.Unit.Converter.SingleNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterSingleNodeTest::RunTest(const FString&)
{
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		BuildSingleNodeDocument(),
		FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("RootWidget type is set"), Data.RootWidget.Type.IsEmpty());
	TestEqual(TEXT("RootWidget name"), Data.RootWidget.Name, FString(TEXT("SingleNode")));

	return true;
}

// ---------------------------------------------------------------------------
// Convert nested nodes
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterNestedNodesTest,
	"Design2UMG.Unit.Converter.NestedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterNestedNodesTest::RunTest(const FString&)
{
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		BuildNestedNodesDocument(),
		FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Root widget name"), Data.RootWidget.Name, FString(TEXT("ParentFrame")));
	TestEqual(TEXT("Parent has nested children"), Data.RootWidget.Children.Num(), 2);
	TestEqual(TEXT("First child fuses SizeBox constraints into Border"), Data.RootWidget.Children[0].Name, FString(TEXT("ChildRect")));
	TestEqual(TEXT("First child uses constrained Border"), Data.RootWidget.Children[0].Type, FString(TEXT("Design2UMGConstrainedBorder")));
	TestEqual(TEXT("Second child fuses SizeBox constraints into TextBlock"), Data.RootWidget.Children[1].Name, FString(TEXT("ChildText")));
	TestEqual(TEXT("Second child uses constrained TextBlock"), Data.RootWidget.Children[1].Type, FString(TEXT("Design2UMGConstrainedTextBlock")));
	TestTrue(TEXT("Fused text widget keeps text payload"), Data.RootWidget.Children[1].Text.Text.IsSet());
	TestEqual(TEXT("Fused text content"), Data.RootWidget.Children[1].Text.Text.GetValue(), FString(TEXT("Nested text")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterTextAutoWrapUsesResolvedLayoutTest,
	"Design2UMG.Unit.Converter.TextAutoWrapUsesResolvedLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterTextAutoWrapUsesResolvedLayoutTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	Doc.Version = TEXT("converter-text-auto-wrap");

	const TSharedPtr<FDesignNode> Root =
		MakeNode(TEXT("text-wrap-root"), TEXT("TextWrapRoot"), EDesignNodeType::Frame, 0.0f, 0.0f, 400.0f, 120.0f);
	const TSharedPtr<FDesignNode> TruncatedJustifiedText =
		MakeNode(TEXT("truncated-justify"), TEXT("TruncatedJustify"), EDesignNodeType::Text, 0.0f, 0.0f, 200.0f, 40.0f);
	TruncatedJustifiedText->Text.Content = TEXT("Truncated justified text");
	TruncatedJustifiedText->Text.FontSize = 16.0f;
	TruncatedJustifiedText->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
	TruncatedJustifiedText->Text.TextAlign = EDesignTextAlign::Justify;
	TruncatedJustifiedText->Text.bTruncateOverflow = true;

	const TSharedPtr<FDesignNode> JustifiedText =
		MakeNode(TEXT("plain-justify"), TEXT("PlainJustify"), EDesignNodeType::Text, 0.0f, 50.0f, 200.0f, 40.0f);
	JustifiedText->Text.Content = TEXT("Plain justified text");
	JustifiedText->Text.FontSize = 16.0f;
	JustifiedText->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;
	JustifiedText->Text.TextAlign = EDesignTextAlign::Justify;

	Root->Children.Add(TruncatedJustifiedText);
	Root->Children.Add(JustifiedText);
	Doc.Children.Add(Root);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TruncatedWidget = FindWidgetById(Data.RootWidget, TEXT("truncated-justify"));
	const FUMGWidgetNode* JustifiedWidget = FindWidgetById(Data.RootWidget, TEXT("plain-justify"));
	if (!TestNotNull(TEXT("Converted truncated justified text exists"), TruncatedWidget)
		|| !TestNotNull(TEXT("Converted plain justified text exists"), JustifiedWidget))
	{
		return false;
	}

	TestFalse(TEXT("Truncated justified text does not enable auto wrap"), TruncatedWidget->Text.bAutoWrapText.Get(false));
	TestFalse(TEXT("Truncated justified text does not emit a wrap width"), TruncatedWidget->Text.WrapTextAt.IsSet());
	TestTrue(TEXT("Plain justified text enables auto wrap"), JustifiedWidget->Text.bAutoWrapText.Get(false));
	TestEqual(TEXT("Plain justified text keeps its wrap width"), JustifiedWidget->Text.WrapTextAt.Get(0.0f), 200.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterPenAuthoredTextBoxOwnerTest,
	"Design2UMG.Unit.Converter.PenAuthoredTextBoxOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterPenAuthoredTextBoxOwnerTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	TestTrue(TEXT("Pen authored textbox fixture parses"), BuildPenAuthoredTextBoxOwnerDocument(Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Owner = FindWidgetById(Data.RootWidget, TEXT("estimated-text"));
	const FUMGWidgetNode* Placeholder = FindWidgetById(Data.RootWidget, TEXT("estimated-text_source_textbox_placeholder"));
	TestTrue(TEXT("Find Pen authored constrained text widget"), Owner != nullptr);
	TestTrue(TEXT("Pen authored text does not create flow placeholder"), Placeholder == nullptr);
	if (!Owner)
	{
		return false;
	}

	TestEqual(TEXT("Pen authored text uses constrained TextBlock"), Owner->Type, FString(TEXT("Design2UMGConstrainedTextBlock")));
	TestTrue(TEXT("Pen authored text owner width is constrained"), Owner->WidgetStyle.WidthOverride.IsSet());
	TestTrue(TEXT("Pen authored text owner height is constrained"), Owner->WidgetStyle.HeightOverride.IsSet());
	TestEqual(TEXT("Pen authored text owner width"), Owner->WidgetStyle.WidthOverride.Get(0.0f), 44.8f);
	TestEqual(TEXT("Pen authored text owner height"), Owner->WidgetStyle.HeightOverride.Get(0.0f), 28.0f);
	TestEqual(TEXT("Pen authored text owner source id"), Owner->Source().SourceNodeId, FString(TEXT("estimated-text")));
	TestEqual(TEXT("Pen authored text appears once"), CountWidgetsById(Data.RootWidget, TEXT("estimated-text")), 1);
	return true;
}

// ---------------------------------------------------------------------------
// Convert empty document
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterEmptyDocTest,
	"Design2UMG.Unit.Converter.EmptyDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterEmptyDocTest::RunTest(const FString&)
{
	FDesignDocument EmptyDoc;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(EmptyDoc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Empty doc produces no root children"), Data.RootWidget.Children.Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Ref expansion
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterRefExpandTest,
	"Design2UMG.Unit.Converter.RefExpand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterRefExpandTest::RunTest(const FString&)
{
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(
		BuildRefExpandDocument(),
		FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Reusable root is skipped from render tree"), Data.RootWidget.Name, FString(TEXT("HostFrame")));
	TestEqual(TEXT("Host frame child count"), Data.RootWidget.Children.Num(), 1);
	TestEqual(TEXT("Expanded child keeps instance name"), Data.RootWidget.Children[0].Name, FString(TEXT("RefNode")));
	TestEqual(TEXT("Expanded child keeps instance id"), Data.RootWidget.Children[0].Id, FString(TEXT("ref-1")));
	TestFalse(TEXT("Expanded child has concrete runtime type"), Data.RootWidget.Children[0].Type.IsEmpty());
	TestNotEqual(TEXT("Expanded child is not a raw ref node"), Data.RootWidget.Children[0].Type, FString(TEXT("Ref")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
