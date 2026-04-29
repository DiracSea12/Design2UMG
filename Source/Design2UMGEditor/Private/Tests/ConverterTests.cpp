// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	static const TCHAR* ConverterPluginName = TEXT("Design2UMG");
	static const FString ConverterFixtureBase = TEXT("Source/Design2UMGEditor/Tests/Fixtures/converter");

	bool LoadConverterFixture(FAutomationTestBase& Test, const FString& FileName, FDesignDocument& OutDoc)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(ConverterPluginName);
		if (!Plugin.IsValid())
		{
			Test.AddError(TEXT("Design2UMG plugin not found"));
			return false;
		}
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), ConverterFixtureBase, FileName);
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
// Convert single node
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConverterSingleNodeTest,
	"Design2UMG.Unit.Converter.SingleNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConverterSingleNodeTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!LoadConverterFixture(*this, TEXT("single_node.json"), Doc))
		return false;

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
	FDesignDocument Doc;
	if (!LoadConverterFixture(*this, TEXT("nested_nodes.json"), Doc))
		return false;

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Root widget name"), Data.RootWidget.Name, FString(TEXT("ParentFrame")));
	TestEqual(TEXT("Parent has nested children"), Data.RootWidget.Children.Num(), 2);
	TestEqual(TEXT("First child is wrapped in SizeBox"), Data.RootWidget.Children[0].Name, FString(TEXT("ChildRect_sz")));
	TestEqual(TEXT("Second child is wrapped in SizeBox"), Data.RootWidget.Children[1].Name, FString(TEXT("ChildText_sz")));
	TestEqual(TEXT("Text wrapper contains one child"), Data.RootWidget.Children[1].Children.Num(), 1);
	TestEqual(TEXT("Inner text widget name"), Data.RootWidget.Children[1].Children[0].Name, FString(TEXT("ChildText")));
	TestTrue(TEXT("Inner text widget keeps text payload"), Data.RootWidget.Children[1].Children[0].Text.bHasText);
	TestEqual(TEXT("Inner text content"), Data.RootWidget.Children[1].Children[0].Text.Text, FString(TEXT("Nested text")));

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
	FDesignDocument Doc;
	if (!LoadConverterFixture(*this, TEXT("ref_expand.json"), Doc))
		return false;

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
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
