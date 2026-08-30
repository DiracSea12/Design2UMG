// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Design2UMGTextWidgetBuilder and text measurement contracts: rich text markup, source font-size conversion (whole/fractional/run), runtime probe subpoint font info.

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGTextWidgetBuilder.h"
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "Dom/JsonObject.h"
#include "Fonts/SlateFontInfo.h"
#include "TextMeasurementRuntimeProbe.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Design2UMGCoreTestHelpers.h"

using namespace Design2UMGCoreTests;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderRichTextMarkupContractTest,
	"Design2UMG.TextWidgetBuilder.RichTextMarkupContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderRichTextMarkupContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Text, 200.0f, 40.0f);
	Node->Text.Content = TEXT("unused");
	Node->Text.FontSize = 16.0f;
	Node->Text.FontFamily = TEXT("Roboto");

	FDesignTextRun& LinkRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	LinkRun.Content = TEXT("A&B <C>");
	LinkRun.Href = TEXT("https://example.test/?q=\"x\"&v='y'");
	LinkRun.bUnderline = true;

	FDesignTextRun& PlainRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	PlainRun.Content = TEXT(" tail");
	PlainRun.FontSize = 18.0f;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Rich text node type"), Widget.Type, FString(TEXT("RichTextBlock")));
	TestEqual(TEXT("Rich text markup keeps escaping contract"),
		Widget.Text.Text.Get(FString()),
		FString(TEXT("<d2u_run_0 href=\"https://example.test/?q=&quot;x&quot;&amp;v=&apos;y&apos;\">A&amp;B &lt;C&gt;</><d2u_run_1> tail</>")));
	TestEqual(TEXT("Single href remains tooltip text"), Widget.WidgetStyle.ToolTipText.Get(FString()), LinkRun.Href);
	TestTrue(TEXT("Style rows still serialize"), Widget.Text.RichTextStyleRowsJson.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderSourceFontSizeConversionContractTest,
	"Design2UMG.TextWidgetBuilder.SourceFontSizeConversionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderSourceFontSizeConversionContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("source_conversion_contract");
	Node->Name = TEXT("Source Conversion Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("Compact label");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 14.0f;
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	Node->Transform.Width = FDesignSize::Fit(0.0f);
	Node->Transform.Height = FDesignSize::Fit(16.0f);
	Node->Transform.bHasWidth = false;
	Node->Transform.bSourceHasWidth = true;
	Node->Transform.bHasHeight = true;
	Node->Transform.bSourceHasHeight = true;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	const float ExpectedFontSize = Node->Text.FontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	TestTrue(TEXT("Builder uses the source px-to-point conversion"),
		FMath::IsNearlyEqual(Widget.TextStyle.FontSize, ExpectedFontSize, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderSourceFontSizeFractionalContractTest,
	"Design2UMG.TextWidgetBuilder.SourceFontSizeFractionalContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderSourceFontSizeFractionalContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("source_fractional_contract");
	Node->Name = TEXT("Source Fractional Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("Fractional label");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 0.5f;
	Node->Text.TextGrowth = EDesignTextGrowth::Auto;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	const float ExpectedFontSize = Node->Text.FontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	TestTrue(TEXT("Builder preserves the exact direct source px-to-point conversion"),
		FMath::IsNearlyEqual(Widget.TextStyle.FontSize, ExpectedFontSize, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementRuntimeProbeSubpointFontInfoContractTest,
	"Design2UMG.TextMeasurement.RuntimeProbeSubpointFontInfoContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementRuntimeProbeSubpointFontInfoContractTest::RunTest(const FString& Parameters)
{
	TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
	Request.FontSize = 0.375f;
	const FSlateFontInfo FontInfo = TextMeasurementRuntimeProbe::BuildFontInfo(Request);
	TestTrue(TEXT("Runtime probe font info preserves 0.375 point sizes"),
		FMath::IsNearlyEqual(FontInfo.Size, 0.375f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderRichTextRunFontSizeConversionContractTest,
	"Design2UMG.TextWidgetBuilder.RichTextRunFontSizeConversionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderRichTextRunFontSizeConversionContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("rich_text_run_source_conversion");
	Node->Name = TEXT("Rich Text Run Source Conversion");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("unused");
	Node->Text.FontFamily = TEXT("Inter");
	Node->Text.FontSize = 0.0f;

	FDesignTextRun& ValidRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	ValidRun.Content = TEXT("valid");
	ValidRun.FontSize = 16.0f;
	FDesignTextRun& InvalidRun = Node->Text.TextRuns.AddDefaulted_GetRef();
	InvalidRun.Content = TEXT("invalid");
	InvalidRun.FontSize = -1.0f;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Explicit run size produces rich text"), Widget.Type, FString(TEXT("RichTextBlock")));
	if (!TestTrue(TEXT("Rich text style rows serialize"), Widget.Text.RichTextStyleRowsJson.IsSet()))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Widget.Text.RichTextStyleRowsJson.GetValue());
	if (!TestTrue(TEXT("Rich text style rows parse"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ValidRow = nullptr;
	const TSharedPtr<FJsonObject>* InvalidRow = nullptr;
	if (!TestTrue(TEXT("Valid run row exists"), RootObject->TryGetObjectField(TEXT("d2u_run_0"), ValidRow) && ValidRow)
		|| !TestTrue(TEXT("Invalid run row exists"), RootObject->TryGetObjectField(TEXT("d2u_run_1"), InvalidRow) && InvalidRow))
	{
		return false;
	}

	double ValidFontSize = 0.0;
	TestTrue(TEXT("Valid run has an applied point size"), (*ValidRow)->TryGetNumberField(UMGPropertyKeys::FontSize, ValidFontSize));
	TestTrue(TEXT("16 source pixels convert to 12 points independently of the invalid parent size"),
		FMath::IsNearlyEqual(static_cast<float>(ValidFontSize), 12.0f, 0.001f));
	TestFalse(TEXT("Invalid explicit run size does not inherit or bypass conversion"),
		(*InvalidRow)->HasField(UMGPropertyKeys::FontSize));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

