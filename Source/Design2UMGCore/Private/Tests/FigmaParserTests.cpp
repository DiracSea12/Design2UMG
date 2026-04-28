// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignSchema.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
	TestFalse(TEXT("SolidNode has fill"), SolidNode->Visual.Fills.IsEmpty() && SolidNode->Visual.Fill.IsNone());

	// Gradient fill child
	const TSharedPtr<FDesignNode>& GradNode = FillsFrame->Children[1];
	TestTrue(TEXT("GradNode valid"), GradNode.IsValid());

	// Image fill child
	const TSharedPtr<FDesignNode>& ImgNode = FillsFrame->Children[2];
	TestTrue(TEXT("ImgNode valid"), ImgNode.IsValid());

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
	// Effects are parsed into Visual.Effects
	TestTrue(TEXT("Has effects"), EffectsFrame->Visual.Effects.Num() > 0);

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
	// boundVariables presence is captured in Metadata or Source
	// We verify the node parsed successfully (non-null, correct type)
	TestEqual(TEXT("VarsFrame type"), VarsFrame->Type, EDesignNodeType::Frame);

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
		AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		TestFalse(TEXT("Empty string returns false"), FFigmaParser::ParseDocument(TEXT(""), Doc));
	}
	{
		AddExpectedError(TEXT("Missing 'document' field"), EAutomationExpectedErrorFlags::Contains, 1);
		FDesignDocument Doc;
		TestFalse(TEXT("Missing document field returns false"), FFigmaParser::ParseDocument(TEXT("{\"foo\":1}"), Doc));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
