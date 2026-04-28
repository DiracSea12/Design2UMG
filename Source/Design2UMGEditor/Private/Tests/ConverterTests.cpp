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
	TestTrue(TEXT("RootWidget has children"), Data.RootWidget.Children.Num() > 0);
	TestTrue(TEXT("Parent has nested children"), Data.RootWidget.Children.Num() >= 2);

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
	// host-1 frame should appear in output (reusable-1 is not a top-level rendered frame)
	TestTrue(TEXT("Output has children"), Data.RootWidget.Children.Num() > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
