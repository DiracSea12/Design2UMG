// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "FigmaParser.h"
#include "DesignSchema.h"

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
	TestTrue(TEXT("Letter spacing preserved"), FMath::IsNearlyEqual(Node->Text.LetterSpacing, 1.5f, 0.001f));
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

#endif // WITH_DEV_AUTOMATION_TESTS
