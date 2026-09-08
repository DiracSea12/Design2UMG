// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Assorted Figma coverage: transform groups, text path fallback, design-only node skipping, exported-image fills, key extraction, and fixture-driven converter runs.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "FigmaImageExporter.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTransformGroupPreservedTest,
	"Design2UMG.FigmaParser.TransformGroupPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTransformGroupPreservedTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 300, 200,
			MakeContainerNode(TEXT("tg1"), TEXT("Transform Group"), TEXT("TRANSFORM_GROUP"), 24, 16, 120, 48,
				MakeLeafNode(TEXT("rect1"), TEXT("Card"), TEXT("RECTANGLE"), 24, 16, 120, 48,
					FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.3, 0.8))))));

	FDesignDocument Doc;
	TestTrue(TEXT("Transform group parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& TransformGroup = Doc.Children[0]->Children[0];
	TestEqual(TEXT("TRANSFORM_GROUP maps to Group"), TransformGroup->Type, EDesignNodeType::Group);
	TestEqual(TEXT("TRANSFORM_GROUP source type preserved"), TransformGroup->Source().SourceNodeType, TEXT("TRANSFORM_GROUP"));
	TestEqual(TEXT("TRANSFORM_GROUP keeps child hierarchy"), TransformGroup->Children.Num(), 1);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* GroupWidget = FindWidgetById(Data.RootWidget, TEXT("tg1"));
	const FUMGWidgetNode* RectWidget = FindWidgetById(Data.RootWidget, TEXT("rect1"));
	// The transform group is a direct child of the document root, inside the flattening
	// pass's preserved depth, so it survives as its own container widget.
	TestTrue(TEXT("Transform group widget exists"), GroupWidget != nullptr);
	TestTrue(TEXT("Transform group child widget exists"), RectWidget != nullptr);
	if (!GroupWidget || !RectWidget)
	{
		return false;
	}

	TestEqual(TEXT("Transform group converts to container widget"), GroupWidget->Type, TEXT("CanvasPanel"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTextPathFallbackTest,
	"Design2UMG.FigmaParser.TextPathFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTextPathFallbackTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 320, 200,
			MakeLeafNode(TEXT("text_path_1"), TEXT("Curved Label"), TEXT("TEXT_PATH"), 32, 24, 180, 40,
				FString::Printf(
					TEXT(",\"characters\":\"Curved Title\",\"fills\":[%s],"
						 "\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":18,\"fontWeight\":400,\"textAlignHorizontal\":\"CENTER\",\"textAlignVertical\":\"TOP\",\"lineHeightPx\":20}"),
					*MakeSolidFillJson(0.9, 0.9, 0.95)))));

	FDesignDocument Doc;
	TestTrue(TEXT("TEXT_PATH parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& TextPathNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("TEXT_PATH maps to Text"), TextPathNode->Type, EDesignNodeType::Text);
	TestEqual(TEXT("TEXT_PATH source type preserved"), TextPathNode->Source().SourceNodeType, TEXT("TEXT_PATH"));
	TestEqual(TEXT("TEXT_PATH keeps text content"), TextPathNode->Text.Content, TEXT("Curved Title"));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("text_path_1"));
	TestTrue(TEXT("TEXT_PATH widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("TEXT_PATH falls back to TextBlock"), TextWidget->Type, TEXT("TextBlock"));
	TestEqual(TEXT("TEXT_PATH fallback keeps content"), GetWidgetProperty(*TextWidget, TEXT("Text")), TEXT("Curved Title"));

	bool bFoundWarning = false;
	for (const FString& Warning : Data.Warnings)
	{
		if (Warning.Contains(TEXT("TEXT_PATH")) && Warning.Contains(TEXT("Curved Label")))
		{
			bFoundWarning = true;
			break;
		}
	}
	TestTrue(TEXT("TEXT_PATH fallback emits warning"), bFoundWarning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSkipDesignOnlyNodesTest,
	"Design2UMG.FigmaParser.SkipDesignOnlyNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSkipDesignOnlyNodesTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("root"), TEXT("Root"), TEXT("FRAME"), 0, 0, 320, 200,
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeLeafNode(TEXT("slice1"), TEXT("Export Slice"), TEXT("SLICE"), 10, 10, 120, 80),
				*MakeLeafNode(TEXT("sticky1"), TEXT("FigJam Sticky"), TEXT("STICKY"), 20, 20, 100, 60),
				*MakeTextNode(TEXT("keep_text"), TEXT("Keep Text"), 32, 24, 90, 24, TEXT("Keep me"),
					TEXT("\"fontFamily\":\"Inter\",\"fontSize\":16,\"fontWeight\":400")))));

	FDesignDocument Doc;
	TestTrue(TEXT("Design-only node fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& KeptNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Only runtime text child remains"), KeptNode->Id, TEXT("keep_text"));
	TestEqual(TEXT("Skipped designer node count recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes.Num(), 2);
	TestEqual(TEXT("First skipped node id recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes[0].NodeId, TEXT("slice1"));
	TestEqual(TEXT("Second skipped node reason recorded"), Doc.ParseDiagnostics().SkippedDesignerNodes[1].Reason, TEXT("figjam_only"));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestNull(TEXT("SLICE node should be skipped"), FindWidgetById(Data.RootWidget, TEXT("slice1")));
	TestNull(TEXT("FigJam STICKY node should be skipped"), FindWidgetById(Data.RootWidget, TEXT("sticky1")));
	TestTrue(TEXT("Normal text node should remain"), FindWidgetById(Data.RootWidget, TEXT("keep_text")) != nullptr);
	bool bFoundSliceWarning = false;
	bool bFoundStickyWarning = false;
	for (const FString& Warning : Data.Warnings)
	{
		bFoundSliceWarning |= Warning.Contains(TEXT("SLICE")) && Warning.Contains(TEXT("slice1"));
		bFoundStickyWarning |= Warning.Contains(TEXT("STICKY")) && Warning.Contains(TEXT("sticky1"));
	}
	TestTrue(TEXT("SLICE skip emits warning"), bFoundSliceWarning);
	TestTrue(TEXT("STICKY skip emits warning"), bFoundStickyWarning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExportedImageClearsExplicitFillTest,
	"Design2UMG.FigmaParser.ExportedImageClearsExplicitFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExportedImageClearsExplicitFillTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("remote_vector");
	Node->Type = EDesignNodeType::Path;
	Node->Visual.bFillExplicitClear = true;

	TMap<FString, FString> IdToUrl;
	IdToUrl.Add(Node->Id, TEXT("https://cdn.example.com/remote-vector.png"));
	TArray<TSharedPtr<FDesignNode>> Nodes;
	Nodes.Add(Node);

	FFigmaImageExporter::ApplyExportedImages(Nodes, IdToUrl);

	TestEqual(TEXT("Exported vector becomes a rectangle"), Node->Type, EDesignNodeType::Rectangle);
	TestFalse(TEXT("Exported image clears the stale explicit fill flag"), Node->Visual.bFillExplicitClear);
	TestEqual(TEXT("Exported vector has one image fill"), Node->Visual.Fills.Num(), 1);
	if (Node->Visual.Fills.Num() == 1)
	{
		TestTrue(TEXT("Exported fill keeps the resolved image URL"), Node->Visual.Fills[0].IsImage());
		TestEqual(TEXT("Exported fill URL is preserved"), Node->Visual.Fills[0].Image.Url, TEXT("https://cdn.example.com/remote-vector.png"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExtractKeyTest,
	"Design2UMG.FigmaParser.ExtractFileKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExtractKeyTest::RunTest(const FString& Parameters)
{
	FString Key;
	TestTrue(TEXT("Design URL"), FFigmaParser::ExtractFileKey(TEXT("https://www.figma.com/design/abc123XYZ/MyFile"), Key));
	TestEqual(TEXT("Design URL key"), Key, TEXT("abc123XYZ"));
	TestTrue(TEXT("File URL"), FFigmaParser::ExtractFileKey(TEXT("https://www.figma.com/file/def456/OtherFile?node-id=1:2"), Key));
	TestEqual(TEXT("File URL key"), Key, TEXT("def456"));
	TestTrue(TEXT("Raw key"), FFigmaParser::ExtractFileKey(TEXT("abc123XYZ"), Key));
	TestEqual(TEXT("Raw key value"), Key, TEXT("abc123XYZ"));
	TestFalse(TEXT("Invalid URL"), FFigmaParser::ExtractFileKey(TEXT("https://example.com/not/figma"), Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComplexConverterTest,
	"Design2UMG.FigmaParser.ComplexEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaComplexConverterTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaComplex fixture"), LoadFixtureJson(TEXT("FigmaComplex.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("E2E parse"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("E2E root type"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("E2E has children"), Data.RootWidget.Children.Num() > 0);
	// Verify that key text widgets survive end-to-end conversion.
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Contains Dashboard Overview"), Texts.Contains(TEXT("Dashboard Overview")));
	TestTrue(TEXT("Contains $48,290"), Texts.Contains(TEXT("$48,290")));
	TestTrue(TEXT("Contains Overview"), Texts.Contains(TEXT("Overview")));
	// CollectImageRefs should capture image references used by the fixture.
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("Has avatar imageRef"), ImageRefs.Contains(TEXT("img_avatar_001")));
	TestTrue(TEXT("Has card bg imageRef"), ImageRefs.Contains(TEXT("img_card_bg_002")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRealDashboardTest,
	"Design2UMG.FigmaParser.RealDashboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRealDashboardTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaRealDashboard fixture"), LoadFixtureJson(TEXT("FigmaRealDashboard.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("Real dashboard parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestTrue(TEXT("Has top-level children"), Doc.Children.Num() > 0);
	if (Doc.Children.Num() == 0)
		return false;
	const auto& Root = Doc.Children[0];
	TestEqual(TEXT("Root type is Frame"), Root->Type, EDesignNodeType::Frame);
	TestTrue(TEXT("Root has children"), Root->Children.Num() > 0);
	// Count node types to validate parser coverage on a real dashboard fixture.
	TMap<EDesignNodeType, int32> TypeCounts;
	TFunction<void(const TSharedPtr<FDesignNode>&)> CountNodes = [&](const TSharedPtr<FDesignNode>& Node)
	{
		TypeCounts.FindOrAdd(Node->Type)++;
		for (const auto& Child : Node->Children)
			CountNodes(Child);
	};
	CountNodes(Root);
	int32 TotalNodes = 0;
	for (const auto& Pair : TypeCounts)
		TotalNodes += Pair.Value;
	TestTrue(TEXT("Total nodes > 50"), TotalNodes > 50);
	TestTrue(TEXT("Has Frame nodes"), TypeCounts.Contains(EDesignNodeType::Frame));
	TestTrue(TEXT("Has Text nodes"), TypeCounts.Contains(EDesignNodeType::Text));
	TestTrue(TEXT("Has Rectangle nodes"), TypeCounts.Contains(EDesignNodeType::Rectangle));
	// Run the full end-to-end conversion on the parsed dashboard.
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Widget root type not empty"), Data.RootWidget.Type.IsEmpty());
	TestTrue(TEXT("Widget has children"), Data.RootWidget.Children.Num() > 0);
	// Verify that text content still exists after conversion.
	TArray<FString> Texts;
	CollectTextValues(Data.RootWidget, Texts);
	TestTrue(TEXT("Has text content"), Texts.Num() > 0);
	return true;
}

#endif

