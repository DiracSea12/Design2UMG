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
	TestEqual(TEXT("SolidNode fill type"), SolidNode->Visual.Fill.Type, EDesignFillType::Color);
	TestEqual(TEXT("SolidNode fills count"), SolidNode->Visual.Fills.Num(), 1);
	TestFalse(TEXT("SolidNode solid color present"), SolidNode->Visual.Fill.SolidColor.IsEmpty());

	// Gradient fill child
	const TSharedPtr<FDesignNode>& GradNode = FillsFrame->Children[1];
	TestTrue(TEXT("GradNode valid"), GradNode.IsValid());
	TestEqual(TEXT("GradNode fill type"), GradNode->Visual.Fill.Type, EDesignFillType::Gradient);
	TestEqual(TEXT("GradNode gradient type"), GradNode->Visual.Fill.Gradient.GradientType, EDesignGradientType::Linear);
	TestEqual(TEXT("GradNode gradient stops"), GradNode->Visual.Fill.Gradient.Colors.Num(), 2);
	TestEqual(TEXT("GradNode first stop position"), GradNode->Visual.Fill.Gradient.Colors[0].Position, 0.0f);
	TestEqual(TEXT("GradNode second stop position"), GradNode->Visual.Fill.Gradient.Colors[1].Position, 1.0f);

	// Image fill child
	const TSharedPtr<FDesignNode>& ImgNode = FillsFrame->Children[2];
	TestTrue(TEXT("ImgNode valid"), ImgNode.IsValid());
	TestEqual(TEXT("ImgNode fill type"), ImgNode->Visual.Fill.Type, EDesignFillType::Image);
	TestEqual(TEXT("ImgNode image ref"), ImgNode->Visual.Fill.Image.Url, FString(TEXT("abc123")));
	TestEqual(TEXT("ImgNode image mode"), ImgNode->Visual.Fill.Image.Mode, EDesignImageMode::Fill);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserGradientGeometryTest,
	"Design2UMG.Gradient.FigmaParserGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserGradientGeometryTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON(
{
  "document": {
    "id": "0:0",
    "name": "Document",
    "type": "DOCUMENT",
    "children": [
      {
        "id": "0:1",
        "name": "Page 1",
        "type": "CANVAS",
        "children": [
          {
            "id": "2:1",
            "name": "RadialGradientNode",
            "type": "RECTANGLE",
            "absoluteBoundingBox": { "x": 0, "y": 0, "width": 120, "height": 80 },
            "fills": [{
              "type": "GRADIENT_RADIAL",
              "gradientHandlePositions": [
                { "x": 0.25, "y": 0.75 },
                { "x": 0.65, "y": 0.75 },
                { "x": 0.25, "y": 1.15 }
              ],
              "gradientStops": [
                { "color": { "r": 1, "g": 0, "b": 0, "a": 1 }, "position": 0 },
                { "color": { "r": 0, "g": 1, "b": 0, "a": 1 }, "position": 0.4 },
                { "color": { "r": 0, "g": 0, "b": 1, "a": 1 }, "position": 1 }
              ]
            }],
            "children": []
          }
        ]
      }
    ]
  },
  "components": {},
  "componentSets": {},
  "styles": {}
}
)JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma document parses"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("One parsed root child"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || !Doc.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestEqual(TEXT("Parsed gradient fill type"), Node->Visual.Fill.Type, EDesignFillType::Gradient);
	TestEqual(TEXT("Parsed gradient type"), Node->Visual.Fill.Gradient.GradientType, EDesignGradientType::Radial);
	TestEqual(TEXT("Parsed gradient stops"), Node->Visual.Fill.Gradient.Colors.Num(), 3);
	TestTrue(TEXT("Parsed radial center present"), Node->Visual.Fill.Gradient.Center.IsSet());
	TestTrue(TEXT("Parsed radial size present"), Node->Visual.Fill.Gradient.Size.IsSet());
	const FVector2D ParsedCenter = Node->Visual.Fill.Gradient.Center.Get(FVector2D::ZeroVector);
	const FVector2D ParsedSize = Node->Visual.Fill.Gradient.Size.Get(FVector2D::ZeroVector);
	TestTrue(TEXT("Parsed radial center X"),
		FMath::IsNearlyEqual(ParsedCenter.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Parsed radial center Y"),
		FMath::IsNearlyEqual(ParsedCenter.Y, 0.75f, 0.0001f));
	TestTrue(TEXT("Parsed radial size width"),
		FMath::IsNearlyEqual(ParsedSize.X, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed radial size height"),
		FMath::IsNearlyEqual(ParsedSize.Y, 0.8f, 0.0001f));
	TestTrue(TEXT("Parsed middle stop position"),
		FMath::IsNearlyEqual(Node->Visual.Fill.Gradient.Colors[1].Position, 0.4f, 0.0001f));
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
	TestFalse(TEXT("BoundVariablesJson captured"), VarsFrame->Source().LayoutExtensions.BoundVariablesJson.IsEmpty());
	TestTrue(
		TEXT("BoundVariablesJson keeps variable alias id"),
		VarsFrame->Source().LayoutExtensions.BoundVariablesJson.Contains(TEXT("VariableID:color/primary")));

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
