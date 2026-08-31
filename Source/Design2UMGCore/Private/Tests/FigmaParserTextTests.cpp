// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignSchema.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "TextFontResolution.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserTextStylePropertiesTest,
	"Design2UMG.Unit.FigmaParser.TextStyleProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserTextStylePropertiesTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON(
{
  "document": {
    "id": "0:0",
    "name": "Doc",
    "type": "DOCUMENT",
    "children": [{
      "id": "0:1",
      "name": "Page 1",
      "type": "CANVAS",
      "children": [{
        "id": "0:2",
        "name": "Headline",
        "type": "TEXT",
        "visible": true,
        "characters": "Parser text",
        "textAutoResize": "TRUNCATE",
        "hyperlink": { "url": "https://example.com/docs" },
        "absoluteBoundingBox": { "x": 10, "y": 20, "width": 220, "height": 48 },
        "fills": [{ "type": "SOLID", "visible": true, "color": { "r": 1, "g": 1, "b": 1, "a": 1 } }],
        "style": {
          "fontFamily": "Inter",
          "fontSize": 24,
          "fontStyle": "Italic",
          "fontWeight": 510,
          "lineHeightUnit": "PIXELS",
          "lineHeightPx": 30,
          "letterSpacing": 1.5,
          "textDecoration": "UNDERLINE",
          "textAlignHorizontal": "JUSTIFIED",
          "textAlignVertical": "BOTTOM"
        }
      }]
    }]
  }
}
)JSON");

	FDesignDocument Document;
	TestTrue(TEXT("Figma text fixture parses"), FFigmaParser::ParseDocument(Json, Document));
	TestEqual(TEXT("One parsed text node"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestEqual(TEXT("Node type"), Node->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Text content"), Node->Text.Content, TEXT("Parser text"));
	TestEqual(TEXT("Font family"), Node->Text.FontFamily, TEXT("Inter"));
	TestEqual(TEXT("Font style"), Node->Text.FontStyle, TEXT("Italic"));
	TestEqual(TEXT("Font weight rounded to hundreds"), Node->Text.FontWeight, TEXT("500"));
	TestTrue(TEXT("Line height stored as ratio"), FMath::IsNearlyEqual(Node->Text.LineHeight, 1.25f, 0.001f));
	TestTrue(TEXT("Letter spacing preserved"), FMath::IsNearlyEqual(Node->Text.LetterSpacing.GetValue(), 1.5f, 0.001f));
	TestTrue(TEXT("Underline mapped"), Node->Text.bUnderline);
	TestFalse(TEXT("Strikethrough remains false"), Node->Text.bStrikethrough);
	TestEqual(TEXT("Hyperlink preserved"), Node->Text.Href, TEXT("https://example.com/docs"));
	TestEqual(TEXT("Horizontal align preserved"), Node->Text.TextAlign, EDesignTextAlign::Justify);
	TestEqual(TEXT("Vertical align preserved"), Node->Text.TextAlignVertical, EDesignTextAlignVertical::Bottom);
	TestEqual(TEXT("Truncate maps to fixed width/height"), Node->Text.TextGrowth, EDesignTextGrowth::FixedWidthHeight);
	TestTrue(TEXT("Truncate is normalized for shared conversion"), Node->Text.bTruncateOverflow);

	const FString* TruncateFlag = Node->Metadata.Find(TEXT("source.text.truncate"));
	TestTrue(TEXT("Truncate metadata recorded"), TruncateFlag != nullptr && *TruncateFlag == TEXT("true"));
	return true;
}

namespace
{
	UFont* MakeItalicProbeFont(const TCHAR* PackageName, const TCHAR* ObjectName, bool bIncludeItalicEntry)
	{
		const FString PackagePath = FString::Printf(TEXT("/Game/Design2UMG/Fonts/%s"), PackageName);
		UPackage* FontPackage = CreatePackage(*PackagePath);
		UFont* Font = NewObject<UFont>(FontPackage, ObjectName, RF_Public | RF_Standalone);
		if (!Font)
		{
			return nullptr;
		}

		Font->FontCacheType = EFontCacheType::Runtime;
		Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
			FName(TEXT("SemiBold")),
			FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
			EFontHinting::Default,
			EFontLoadingPolicy::LazyLoad));
		if (bIncludeItalicEntry)
		{
			Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
				FName(TEXT("SemiBold Italic")),
				FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
				EFontHinting::Default,
				EFontLoadingPolicy::LazyLoad));
		}
		return Font;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserTextItalicTypefaceTest,
	"Design2UMG.Unit.FigmaParser.TextItalicTypeface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserTextItalicTypefaceTest::RunTest(const FString&)
{
	FFontResolver::ClearCache();

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!TestTrue(TEXT("Settings available"), Settings != nullptr))
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	Settings->ExplicitFontMappings.Empty();

	UFont* Font = MakeItalicProbeFont(TEXT("D2U_FigmaItalicTypefaceTest"), TEXT("D2U_FigmaItalicTypefaceTest"), true);
	if (!TestNotNull(TEXT("Italic probe font created"), Font))
	{
		Settings->ExplicitFontMappings = SavedMappings;
		return false;
	}
	Settings->ExplicitFontMappings.Add(
		TEXT("D2UFigmaItalicProbe"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(Font->GetPathName())));
	FFontResolver::ClearCache();

	const FString Json = TEXT(R"JSON({
  "document": {
    "id": "0:0",
    "name": "Doc",
    "type": "DOCUMENT",
    "children": [{
      "id": "0:1",
      "name": "Page 1",
      "type": "CANVAS",
      "children": [{
        "id": "0:2",
        "name": "Caption",
        "type": "TEXT",
        "characters": "Italic probe",
        "absoluteBoundingBox": { "x": 0, "y": 0, "width": 120, "height": 24 },
        "style": {
          "fontFamily": "D2UFigmaItalicProbe",
          "fontPostScriptName": "D2UFigmaItalicProbe-SemiBoldItalic",
          "fontSize": 16,
          "fontStyle": "Semi Bold Italic",
          "fontWeight": 600
        }
      }]
    }]
  }
}
)JSON");

	FDesignDocument Document;
	TestTrue(TEXT("Figma italic fixture parses"), FFigmaParser::ParseDocument(Json, Document));
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		Settings->ExplicitFontMappings = SavedMappings;
		FFontResolver::ClearCache();
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestEqual(TEXT("Font style kept verbatim"), Node->Text.FontStyle, TEXT("Semi Bold Italic"));
	TestEqual(TEXT("Font weight normalized"), Node->Text.FontWeight, TEXT("600"));

	const TextFontResolution::FResolvedFontSelection ItalicFont = TextFontResolution::Resolve(
		Node->Text.FontFamily,
		Node->Text.FontPostScriptName,
		Node->Text.FontWeight,
		Node->Text.FontStyle);
	TestEqual(TEXT("Italic style selects the italic typeface entry"), ItalicFont.TypefaceName, TEXT("SemiBold Italic"));

	TestEqual(
		TEXT("Non-italic style token keeps the roman typeface"),
		TextFontResolution::Resolve(Node->Text.FontFamily, Node->Text.FontPostScriptName, Node->Text.FontWeight, TEXT("Bold")).TypefaceName,
		TEXT("SemiBold"));
	TestEqual(
		TEXT("Empty style keeps the roman typeface"),
		TextFontResolution::Resolve(Node->Text.FontFamily, Node->Text.FontPostScriptName, Node->Text.FontWeight).TypefaceName,
		TEXT("SemiBold"));

	Settings->ExplicitFontMappings = SavedMappings;
	FFontResolver::ClearCache();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserTextItalicFallbackWarningTest,
	"Design2UMG.Unit.FigmaParser.TextItalicFallbackWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaParserTextItalicFallbackWarningTest::RunTest(const FString&)
{
	FFontResolver::ClearCache();
	AddExpectedError(
		TEXT("Font 'D2UFigmaRomanProbe' has no italic typeface 'SemiBold Italic'; falling back to roman"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!TestTrue(TEXT("Settings available"), Settings != nullptr))
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	Settings->ExplicitFontMappings.Empty();

	UFont* Font = MakeItalicProbeFont(TEXT("D2U_FigmaItalicFallbackTest"), TEXT("D2U_FigmaItalicFallbackTest"), false);
	if (!TestNotNull(TEXT("Roman-only probe font created"), Font))
	{
		Settings->ExplicitFontMappings = SavedMappings;
		return false;
	}
	Settings->ExplicitFontMappings.Add(
		TEXT("D2UFigmaRomanProbe"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(Font->GetPathName())));
	FFontResolver::ClearCache();

	const TextFontResolution::FResolvedFontSelection FirstPass = TextFontResolution::Resolve(
		TEXT("D2UFigmaRomanProbe"), FString(), TEXT("600"), TEXT("Italic"));
	TestEqual(TEXT("Missing italic entry falls back to the roman typeface"), FirstPass.TypefaceName, TEXT("SemiBold"));

	const TextFontResolution::FResolvedFontSelection SecondPass = TextFontResolution::Resolve(
		TEXT("D2UFigmaRomanProbe"), FString(), TEXT("600"), TEXT("SemiBold Italic"));
	TestEqual(TEXT("Repeated italic fallback keeps the roman typeface"), SecondPass.TypefaceName, TEXT("SemiBold"));

	Settings->ExplicitFontMappings = SavedMappings;
	FFontResolver::ClearCache();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
