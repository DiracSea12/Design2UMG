// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGLog.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"
#include "PenParser.h"
#include "Misc/OutputDevice.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetNodeAccess.h"

#if WITH_AUTOMATION_WORKER
namespace
{
	class FDesign2UMGLogCapture final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (V != nullptr && Category == FName(TEXT("LogDesign2UMG")))
			{
				Captured += V;
				Captured += TEXT("\n");
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override
		{
			return true;
		}

		virtual bool CanBeUsedOnPanicThread() const override
		{
			return true;
		}

		const FString& GetCaptured() const
		{
			return Captured;
		}

	private:
		FString Captured;
	};

	const FUMGWidgetNode* FindWidgetByIdRecursive(const FUMGWidgetNode& Root, const FString& Id)
	{
		if (Root.Id == Id)
		{
			return &Root;
		}

		for (const FUMGWidgetNode& Child : Root.Children)
		{
			if (const FUMGWidgetNode* Match = FindWidgetByIdRecursive(Child, Id))
			{
				return Match;
			}
		}

		return nullptr;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProceduralResourceRequestBuildsTypedRequestTest,
	"Design2UMG.WidgetNodeAccess.ProceduralResourceRequestBuildsTypedRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FProceduralResourceRequestBuildsTypedRequestTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Brush.Gradient.Type = TEXT("Radial");
	Node.Brush.Gradient.Center = FVector2D(0.25f, 0.75f);
	Node.Brush.Gradient.Size = FVector2D(0.8f, 0.6f);
	Node.Brush.Gradient.Rotation = 32.0f;
	Node.Brush.Gradient.Stops = {
		{ FLinearColor::Red, 0.0f },
		{ FLinearColor::Blue, 1.0f }
	};
	Node.WidgetStyle.WidthOverride = 64.0f;
	Node.WidgetStyle.HeightOverride = 32.0f;
	Node.Shadow.ShadowBlur = 12.0f;
	Node.Brush.BrushColor = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Node.Brush.MaterialParams = TEXT("{\"ColorCenter\":\"#01020304\",\"ColorEdge\":\"#05060708\"}");

	const FString DescriptorMaterialParams =
		Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
	TestTrue(TEXT("Gradient params serialize ahead of explicit params"),
		!DescriptorMaterialParams.IsEmpty());

	FUMGProceduralResourceRequest Request;
	TestTrue(
		TEXT("Typed-first procedural request can be built from descriptor-backed brush data"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request));
	TestEqual(TEXT("Gradient descriptor resolves to material ref"), Request.MaterialRef, FString(TEXT("radialgradient")));
	TestTrue(TEXT("Gradient descriptor resolves material params"), Request.MaterialParams.IsSet());
	TestEqual(TEXT("Request material params match typed descriptor"), Request.MaterialParams.Get(FString()), DescriptorMaterialParams);
	TestTrue(TEXT("Width override preserved"), Request.WidthOverride.IsSet() && FMath::IsNearlyEqual(Request.WidthOverride.GetValue(), 64.0f));
	TestTrue(TEXT("Height override preserved"), Request.HeightOverride.IsSet() && FMath::IsNearlyEqual(Request.HeightOverride.GetValue(), 32.0f));
	TestTrue(TEXT("Shadow blur preserved"), Request.ShadowBlur.IsSet() && FMath::IsNearlyEqual(Request.ShadowBlur.GetValue(), 12.0f));
	const FLinearColor ExpectedTypedBrushColor = Node.Brush.BrushColor.GetValue();
	TestTrue(TEXT("Brush color preserved through typed request serialization"),
		Request.BrushColor.IsSet() && Request.BrushColor.GetValue().Equals(ExpectedTypedBrushColor, 0.0001f));

	TestTrue(TEXT("Typed request keeps radial center"), Request.MaterialParams.GetValue().Contains(TEXT("\"CenterX\":0.2500")));
	TestEqual(TEXT("Typed request width override value"), Request.WidthOverride.GetValue(), 64.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaTextLetterSpacingPresenceExplicitZeroVsUnsetTest,
	"Design2UMG.FigmaParser.TextLetterSpacingPresenceExplicitZeroVsUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTextLetterSpacingPresenceExplicitZeroVsUnsetTest::RunTest(const FString& Parameters)
{
	const FString ExplicitZeroJson = TEXT(R"JSON(
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
        "id": "explicit_zero",
        "name": "Explicit Zero",
        "type": "TEXT",
        "visible": true,
        "characters": "Spacing",
        "absoluteBoundingBox": { "x": 10, "y": 20, "width": 220, "height": 48 },
        "fills": [{ "type": "SOLID", "visible": true, "color": { "r": 1, "g": 1, "b": 1, "a": 1 } }],
        "style": {
          "fontFamily": "Inter",
          "fontSize": 24,
          "fontWeight": 400,
          "letterSpacing": 0
        }
      }]
    }]
  }
}
)JSON");

	const FString UnsetJson = TEXT(R"JSON(
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
        "id": "unset_spacing",
        "name": "Unset Spacing",
        "type": "TEXT",
        "visible": true,
        "characters": "Spacing",
        "absoluteBoundingBox": { "x": 10, "y": 20, "width": 220, "height": 48 },
        "fills": [{ "type": "SOLID", "visible": true, "color": { "r": 1, "g": 1, "b": 1, "a": 1 } }],
        "style": {
          "fontFamily": "Inter",
          "fontSize": 24,
          "fontWeight": 400
        }
      }]
    }]
  }
}
)JSON");

	FDesignDocument ExplicitZeroDocument;
	FDesignDocument UnsetDocument;
	TestTrue(TEXT("Explicit-zero Figma text fixture parses"), FFigmaParser::ParseDocument(ExplicitZeroJson, ExplicitZeroDocument));
	TestTrue(TEXT("Unset Figma text fixture parses"), FFigmaParser::ParseDocument(UnsetJson, UnsetDocument));
	if (ExplicitZeroDocument.Children.Num() != 1 || UnsetDocument.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& ExplicitZeroNode = ExplicitZeroDocument.Children[0];
	const TSharedPtr<FDesignNode>& UnsetNode = UnsetDocument.Children[0];
	TestTrue(TEXT("Explicit-zero letter spacing keeps presence"), ExplicitZeroNode->Text.LetterSpacing.IsSet());
	TestTrue(TEXT("Explicit-zero letter spacing value stays zero"), FMath::IsNearlyZero(ExplicitZeroNode->Text.LetterSpacing.GetValue()));
	TestFalse(TEXT("Unset letter spacing keeps absence"), UnsetNode->Text.LetterSpacing.IsSet());

	const FUMGWidgetData ExplicitZeroData = FDesign2UMGConverter::Convert(ExplicitZeroDocument, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetData UnsetData = FDesign2UMGConverter::Convert(UnsetDocument, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Explicit-zero conversion omits redundant zero-valued runtime letter spacing"),
		ExplicitZeroData.RootWidget.TextStyle.LetterSpacing.IsSet());
	TestFalse(TEXT("Unset conversion does not emit typed letter spacing"), UnsetData.RootWidget.TextStyle.LetterSpacing.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFigmaParserMalformedJsonFailsCleanlyTest,
	"Design2UMG.FigmaParser.MalformedJsonFailsCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaParserMalformedJsonFailsCleanlyTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Invalid Figma export JSON"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Failed to parse JSON"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesignDocument Document;
	FDesign2UMGLogCapture Capture;
	if (GLog != nullptr)
	{
		GLog->AddOutputDevice(&Capture);
	}
	const bool bParsed = FFigmaParser::ParseDocument(TEXT("{\"document\":"), Document);
	if (GLog != nullptr)
	{
		GLog->FlushThreadedLogs();
		GLog->Flush();
		GLog->RemoveOutputDevice(&Capture);
	}
	TestFalse(TEXT("Malformed Figma JSON is rejected"), bParsed);
	TestEqual(TEXT("Malformed Figma JSON does not append children"), Document.Children.Num(), 0);
	TestTrue(TEXT("Malformed Figma JSON logs reader error context"), Capture.GetCaptured().Contains(TEXT("Reader error:")));
	TestTrue(TEXT("Malformed Figma JSON logs reader line context"), Capture.GetCaptured().Contains(TEXT("Line:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenParserDeepNestingGuardRejectsOverflowTest,
	"Design2UMG.PenParser.DeepNestingGuardRejectsOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenParserDeepNestingGuardRejectsOverflowTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Pen document children nesting is too deep"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Design file parsing failed because node nesting exceeds"), EAutomationExpectedErrorFlags::Contains, 1);
	FString Json = TEXT("{\"children\":[");
	for (int32 Index = 0; Index < 520; ++Index)
	{
		Json += FString::Printf(
			TEXT("{\"id\":\"node_%d\",\"type\":\"frame\",\"children\":["),
			Index);
	}
	Json += TEXT("{\"id\":\"leaf\",\"type\":\"frame\"}");
	for (int32 Index = 0; Index < 520; ++Index)
	{
		Json += TEXT("]}");
	}
	Json += TEXT("]}");

	FDesignDocument Document;
	TestFalse(TEXT("Over-deep Pen JSON fails cleanly without stack overflow"), FPenParser::ParseDocument(Json, Document));
	TestTrue(TEXT("Parser reports failure instead of appending a truncated top-level tree"), Document.Children.Num() <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenRefOverrideExplicitZeroLetterSpacingPresenceTest,
	"Design2UMG.PenParser.RefOverrideExplicitZeroLetterSpacingPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenRefOverrideExplicitZeroLetterSpacingPresenceTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "label_component",
				"type": "frame",
				"reusable": true,
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Default",
						"fontFamily": "Inter",
						"fontSize": 16
					}
				]
			},
			{
				"id": "screen",
				"type": "frame",
				"children": [
					{
						"id": "label_instance",
						"type": "ref",
						"ref": "label_component",
						"descendants": {
							"label": {
								"content": "Override",
								"letterSpacing": 0
							}
						}
					}
				]
			}
		]
	})JSON");

	FDesignDocument Document;
	TestTrue(TEXT("Pen ref override fixture parses"), FPenParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* LabelWidget = FindWidgetByIdRecursive(Data.RootWidget, TEXT("label"));
	if (!TestNotNull(TEXT("Expanded override label exists"), LabelWidget))
	{
		return false;
	}

	TestFalse(TEXT("Expanded override omits redundant zero-valued runtime letter spacing"),
		LabelWidget->TextStyle.LetterSpacing.IsSet());
	return true;
}

#endif // WITH_AUTOMATION_WORKER
