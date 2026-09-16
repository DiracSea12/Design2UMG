// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignSchema.h"
#include "FigmaTestDocBuilder.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "Serialization/JsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "Design2UMGConverter.h"
#include "Containers/StringConv.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCharacterLinkDiagnosticTest,
	"Design2UMG.Unit.FigmaParser.CharacterLinkDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaCharacterLinkDiagnosticTest::RunTest(const FString&)
{
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(Design2UMGCoreTests::MakeTextNode(
		TEXT("0:2"), TEXT("Links"), 0, 0, 180, 40, TEXT("AB"), TEXT("\"fontSize\":16"),
		TEXT(R"JSON(,"characterStyleOverrides":[1,2],"styleOverrideTable":{"1":{"hyperlink":{"type":"URL","url":"https://example.com"}},"2":{"hyperlink":{"type":"NODE","nodeID":"9:2"}}})JSON")));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Link styles parse"), FFigmaParser::ParseDocument(Json, Document))
		|| !TestEqual(TEXT("Single text node"), Document.Children.Num(), 1)) return false;
	const auto& Text = Document.Children[0]->Text;
	if (!TestEqual(TEXT("Separate link spans"), Text.TextRuns.Num(), 2)) return false;
	TestEqual(TEXT("URL retained"), Text.TextRuns[0].Href, FString(TEXT("https://example.com")));
	TestTrue(TEXT("Node link is explicitly diagnosed"), Text.ParseWarnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("Character style 2")) && Warning.Contains(TEXT("node-target links")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCharacterStylePaintDataTest,
	"Design2UMG.Unit.FigmaParser.CharacterStylePaintData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStylePaintDataTest::RunTest(const FString&)
{
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(
		Design2UMGCoreTests::MakeTextNode(TEXT("0:2"), TEXT("Paint spans"), 0, 0, 180, 40,
			TEXT("ABCD"), TEXT("\"fontSize\":16"),
			TEXT(R"JSON(,"characterStyleOverrides":[0,1,2,3],"styleOverrideTable":{
			"1":{"fills":[{"type":"GRADIENT_LINEAR","opacity":0.5,"blendMode":"MULTIPLY",
			"gradientHandlePositions":[{"x":0,"y":0},{"x":1,"y":0},{"x":0,"y":1}],
			"gradientStops":[{"position":0,"color":{"r":1,"g":0,"b":0,"a":1}},{"position":1,"color":{"r":0,"g":0,"b":1,"a":1}}]},
			{"type":"SOLID","color":{"r":0,"g":1,"b":0},"opacity":0.25}]},
			"2":{"fills":[]},"3":{"fontSize":24}})JSON")));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Paint document parses"), FFigmaParser::ParseDocument(Json, Document))
		|| !TestEqual(TEXT("One paint node"), Document.Children.Num(), 1)) return false;
	const TArray<FDesignTextRun>& Runs = Document.Children[0]->Text.TextRuns;
	if (!TestEqual(TEXT("Four distinct runs"), Runs.Num(), 4)) return false;
	TestFalse(TEXT("Default run inherits fills"), Runs[0].bHasFillOverride);
	TestTrue(TEXT("Paint run has explicit fills"), Runs[1].bHasFillOverride);
	if (!TestEqual(TEXT("Both ordered layers survive parsing"), Runs[1].Fills.Num(), 2)) return false;
	TestTrue(TEXT("Gradient remains first layer"), Runs[1].Fills[0].Type == EDesignFillType::Gradient);
	TestTrue(TEXT("Solid remains second layer"), Runs[1].Fills[1].Type == EDesignFillType::Color);
	TestEqual(TEXT("Gradient opacity retained"), Runs[1].Fills[0].Opacity, 0.5f);
	TestEqual(TEXT("Blend mode retained"), Runs[1].Fills[0].BlendMode, FString(TEXT("MULTIPLY")));
	TestEqual(TEXT("Gradient stops retained"), Runs[1].Fills[0].Gradient.Colors.Num(), 2);
	TestTrue(TEXT("Explicit empty is not inheritance"), Runs[2].bHasFillOverride && Runs[2].Fills.IsEmpty());
	TestFalse(TEXT("Font-only override inherits fills"), Runs[3].bHasFillOverride);
	TArray<uint8> Encoded;
	FDesignTextRun Original = Runs[1];
	FMemoryWriter Writer(Encoded);
	FDesignTextRun::StaticStruct()->SerializeBin(Writer, &Original);
	if (!TestFalse(TEXT("Paint run serializes"), Writer.IsError())) return false;
	FDesignTextRun Reloaded;
	FMemoryReader Reader(Encoded);
	FDesignTextRun::StaticStruct()->SerializeBin(Reader, &Reloaded);
	if (!TestFalse(TEXT("Paint run deserializes"), Reader.IsError())) return false;
	TestTrue(TEXT("Override presence survives round trip"), Reloaded.bHasFillOverride);
	if (!TestEqual(TEXT("Paint layers survive round trip"), Reloaded.Fills.Num(), 2)) return false;
	TestEqual(TEXT("Round trip preserves blend"), Reloaded.Fills[0].BlendMode, Runs[1].Fills[0].BlendMode);
	TestTrue(TEXT("Rendering limitation remains reported until implemented"), !Document.Children[0]->Text.ParseWarnings.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaCharacterStyleOverrideRunsTest,
	"Design2UMG.Unit.FigmaParser.CharacterStyleOverrideRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStyleOverrideRunsTest::RunTest(const FString&)
{
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(
		Design2UMGCoreTests::MakeTextNode(
			TEXT("0:2"), TEXT("Mixed text"), 0, 0, 180, 40, TEXT("ABCD"),
			TEXT("\"fontFamily\":\"Base Family\",\"fontSize\":16,\"fontWeight\":400"),
			TEXT(",\"characterStyleOverrides\":[0,1,1],"
				"\"styleOverrideTable\":{\"1\":{\"fontFamily\":\"Accent Family\","
				"\"fontSize\":24,\"fontWeight\":700,\"italic\":true}}")));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Document parses"), FFigmaParser::ParseDocument(Json, Document))
		|| !TestEqual(TEXT("One text node"), Document.Children.Num(), 1))
	{
		return false;
	}
	const TArray<FDesignTextRun>& Runs = Document.Children[0]->Text.TextRuns;
	if (!TestEqual(TEXT("Contiguous style spans remain separate"), Runs.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("First default span"), Runs[0].Content, TEXT("A"));
	TestEqual(TEXT("Overridden span"), Runs[1].Content, TEXT("BC"));
	TestEqual(TEXT("Final default span"), Runs[2].Content, TEXT("D"));
	TestEqual(TEXT("Overridden family"), Runs[1].FontFamily, TEXT("Accent Family"));
	TestEqual(TEXT("Overridden weight"), Runs[1].FontWeight, TEXT("700"));
	TestEqual(TEXT("Italic flag becomes font style"), Runs[1].FontStyle, TEXT("Italic"));
	TestEqual(TEXT("Overridden size"), Runs[1].FontSize.Get(0.0f), 24.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaCharacterStyleColorTest,
	"Design2UMG.Unit.FigmaParser.CharacterStyleColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStyleColorTest::RunTest(const FString&)
{
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(
		Design2UMGCoreTests::MakeTextNode(
			TEXT("0:2"), TEXT("Mixed color"), 0, 0, 180, 40, TEXT("AB"),
			TEXT("\"fontSize\":16"),
			TEXT(",\"characterStyleOverrides\":[0,1],"
				"\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":1}}],"
				"\"styleOverrideTable\":{\"1\":{\"fills\":[{\"type\":\"SOLID\","
				"\"opacity\":0.5,\"color\":{\"r\":1,\"g\":0,\"b\":0}}]}}")));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Color document parses"), FFigmaParser::ParseDocument(Json, Document))
		|| !TestEqual(TEXT("One color node"), Document.Children.Num(), 1))
	{
		return false;
	}
	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Document.Children[0]);
	if (!TestTrue(TEXT("Color-only override produces style rows"), Widget.Text.RichTextStyleRowsJson.IsSet()))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Rows;
	if (!TestTrue(TEXT("Style rows parse"), FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(Widget.Text.RichTextStyleRowsJson.GetValue()), Rows)))
	{
		return false;
	}
	const FString DefaultColor = Rows->GetObjectField(TEXT("d2u_run_0"))->GetStringField(UMGPropertyKeys::Color);
	const FString OverrideColor = Rows->GetObjectField(TEXT("d2u_run_1"))->GetStringField(UMGPropertyKeys::Color);
	TestNotEqual(TEXT("Run color differs from base color"), OverrideColor, DefaultColor);
	const FLinearColor AppliedColor = FUMGValueParser::ParseColor(OverrideColor);
	TestTrue(TEXT("Override retains red"), FMath::IsNearlyEqual(AppliedColor.R, 1.0f));
	TestTrue(TEXT("Override retains half opacity within source byte precision"),
		FMath::IsNearlyEqual(AppliedColor.A, 0.5f, 1.0f / 255.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaCharacterStyleDiagnosticTest,
	"Design2UMG.Unit.FigmaParser.CharacterStyleDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStyleDiagnosticTest::RunTest(const FString&)
{
	const TArray<FString> Inputs = {
		TEXT(",\"characterStyleOverrides\":\"invalid\",\"styleOverrideTable\":{}"),
		TEXT(",\"characterStyleOverrides\":[0,1,0],\"styleOverrideTable\":{\"1\":{\"fontSize\":24}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{}"),
		TEXT(",\"characterStyleOverrides\":[0,1.5],\"styleOverrideTable\":{}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"paragraphSpacing\":12}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"lineHeightUnit\":\"PIXELS\",\"lineHeightPx\":32}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"fontSize\":\"large\"}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"fontSize\":-2}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"fills\":false}}"),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"lineHeightUnit\":\"FONT_SIZE_%\",\"lineHeightPercentFontSize\":150}}")
	};
	for (const FString& Extra : Inputs)
	{
		FDesignDocument Document;
		const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(
			Design2UMGCoreTests::MakeTextNode(TEXT("0:2"), TEXT("Broken span"), 0, 0, 180, 40,
				TEXT("AB"), TEXT("\"fontSize\":16"), Extra));
		if (!TestTrue(TEXT("Diagnostic document parses"), FFigmaParser::ParseDocument(Json, Document)))
		{
			return false;
		}
		if (Document.Children[0]->Text.ParseWarnings.ContainsByPredicate([](const FString& Warning)
			{ return Warning.StartsWith(TEXT("Invalid character style")); }))
		{
			TestTrue(TEXT("Invalid later style does not publish a partial text-run list"), Document.Children[0]->Text.TextRuns.IsEmpty());
			TestEqual(TEXT("Invalid style retains the complete source text"), Document.Children[0]->Text.Content, FString(TEXT("AB")));
		}
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		TestTrue(TEXT("Incomplete character styles identify the affected node in import warnings"),
			Data.Warnings.ContainsByPredicate([](const FString& Warning)
			{
				return Warning.Contains(TEXT("0:2")) && Warning.Contains(TEXT("character style"));
			}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCharacterStyleUnicodeTest,
	"Design2UMG.Unit.FigmaParser.CharacterStyleUnicode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStyleUnicodeTest::RunTest(const FString&)
{
	// JSON escape pairs ensure the fixture tests UTF-16 indexing, not source-file encoding.
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(TEXT(R"JSON(
		{"id":"unicode","type":"TEXT","characters":"A\uD83D\uDE00B","style":{"fontSize":16},
		"characterStyleOverrides":[0,1,2,0],"styleOverrideTable":{"1":{"fontSize":20},"2":{"fontSize":24}}})JSON"));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Unicode document parses"), FFigmaParser::ParseDocument(Json, Document)) ||
		!TestEqual(TEXT("Unicode text node exists"), Document.Children.Num(), 1)) return false;
	TestEqual(TEXT("Fixture contains four UTF-16 units"), FTCHARToUTF16(*Document.Children[0]->Text.Content).Length(), 4);
	TestTrue(TEXT("Splitting a surrogate pair reports affected style boundary"),
		Document.Children[0]->Text.ParseWarnings.ContainsByPredicate([](const FString& Warning)
		{ return Warning.Contains(TEXT("character style")) && Warning.Contains(TEXT("surrogate")); }));
	TestTrue(TEXT("Invalid boundary does not emit broken Unicode spans"), Document.Children[0]->Text.TextRuns.IsEmpty());
	FString ValidJson = Json.Replace(TEXT("[0,1,2,0]"), TEXT("[0,1,1,0]"));
	FDesignDocument ValidDocument;
	if (!TestTrue(TEXT("Valid Unicode spans parse"), FFigmaParser::ParseDocument(ValidJson, ValidDocument))) return false;
	const auto& Runs = ValidDocument.Children[0]->Text.TextRuns;
	if (!TestEqual(TEXT("Emoji stays in one span"), Runs.Num(), 3)) return false;
	TestEqual(TEXT("Emoji retains both UTF-16 code units"), FTCHARToUTF16(*Runs[1].Content).Length(), 2);
	TestTrue(TEXT("Valid surrogate boundary has no warning"), ValidDocument.Children[0]->Text.ParseWarnings.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCharacterStyleNullPostScriptTest,
	"Design2UMG.Unit.FigmaParser.CharacterStyleNullPostScript", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaCharacterStyleNullPostScriptTest::RunTest(const FString&)
{
	const FString Json = Design2UMGCoreTests::MakeFigmaDocJson(Design2UMGCoreTests::MakeTextNode(
		TEXT("null-font"), TEXT("Optional font identity"), 0, 0, 180, 40, TEXT("AB"),
		TEXT("\"fontSize\":16,\"fontFamily\":\"Base\""),
		TEXT(",\"characterStyleOverrides\":[0,1],\"styleOverrideTable\":{\"1\":{\"fontFamily\":\"Accent\",\"fontPostScriptName\":null,\"fontSize\":24}}")));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Null optional font identity parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	const auto& Text = Document.Children[0]->Text;
	TestTrue(TEXT("Null PostScript name is not a malformed style"), Text.ParseWarnings.IsEmpty());
	if (!TestEqual(TEXT("Other overrides survive an unavailable PostScript name"), Text.TextRuns.Num(), 2)) return false;
	TestEqual(TEXT("Family still applies"), Text.TextRuns[1].FontFamily, FString(TEXT("Accent")));
	TestEqual(TEXT("Size still applies"), Text.TextRuns[1].FontSize.Get(0.0f), 24.0f);
	TestTrue(TEXT("No exact face identity is invented"), Text.TextRuns[1].FontPostScriptName.IsEmpty());
	return true;
}

#endif
