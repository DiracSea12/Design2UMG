// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace
{
	TSharedPtr<FDesignNode> MakeNode(
		const FString& Id,
		const FString& Name,
		const EDesignNodeType Type,
		const float X,
		const float Y,
		const float Width,
		const float Height)
	{
		const TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Name;
		Node->Type = Type;
		Node->Transform.SetPosition(X, Y);
		Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		return Node;
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
		Rect->Visual.Fill = FDesignFill::FromColor(TEXT("#ff0000"));

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

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildSingleNodeDocument());
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildNestedNodesDocument());
	TestEqual(TEXT("Root widget name"), Data.RootWidget.Name, FString(TEXT("ParentFrame")));
	TestEqual(TEXT("Parent has nested children"), Data.RootWidget.Children.Num(), 2);
	TestEqual(TEXT("First child is wrapped in SizeBox"), Data.RootWidget.Children[0].Name, FString(TEXT("ChildRect_sz")));
	TestEqual(TEXT("Second child is wrapped in SizeBox"), Data.RootWidget.Children[1].Name, FString(TEXT("ChildText_sz")));
	TestEqual(TEXT("Text wrapper contains one child"), Data.RootWidget.Children[1].Children.Num(), 1);
	TestEqual(TEXT("Inner text widget name"), Data.RootWidget.Children[1].Children[0].Name, FString(TEXT("ChildText")));
	TestTrue(TEXT("Inner text widget keeps text payload"), Data.RootWidget.Children[1].Children[0].Text.Text.IsSet());
	TestEqual(TEXT("Inner text content"), Data.RootWidget.Children[1].Children[0].Text.Text.GetValue(), FString(TEXT("Nested text")));

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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(EmptyDoc);
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
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(BuildRefExpandDocument());
	TestEqual(TEXT("Reusable root is skipped from render tree"), Data.RootWidget.Name, FString(TEXT("HostFrame")));
	TestEqual(TEXT("Host frame child count"), Data.RootWidget.Children.Num(), 1);
	TestEqual(TEXT("Expanded child is wrapped in SizeBox"), Data.RootWidget.Children[0].Name, FString(TEXT("RefNode_sz")));
	TestEqual(TEXT("Expanded child wrapper id"), Data.RootWidget.Children[0].Id, FString(TEXT("ref-1_sz")));
	TestEqual(TEXT("Expanded wrapper contains one child"), Data.RootWidget.Children[0].Children.Num(), 1);
	TestEqual(TEXT("Expanded inner widget keeps instance name"), Data.RootWidget.Children[0].Children[0].Name, FString(TEXT("RefNode")));
	TestEqual(TEXT("Expanded inner widget keeps instance id"), Data.RootWidget.Children[0].Children[0].Id, FString(TEXT("ref-1")));
	TestFalse(TEXT("Expanded inner widget has concrete runtime type"), Data.RootWidget.Children[0].Children[0].Type.IsEmpty());
	TestNotEqual(TEXT("Expanded inner widget is not a raw ref node"), Data.RootWidget.Children[0].Children[0].Type, FString(TEXT("Ref")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
