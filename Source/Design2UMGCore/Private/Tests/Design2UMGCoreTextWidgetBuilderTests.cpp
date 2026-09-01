// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Design2UMGTextWidgetBuilder and text measurement contracts: rich text markup, source font-size conversion (whole/fractional/run), runtime probe subpoint font info.

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGTextWidgetBuilder.h"
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "Design2UMGSettings.h"
#include "Dom/JsonObject.h"
#include "Fonts/SlateFontInfo.h"
#include "FontResolver.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderOverflowClippingContractTest,
	"Design2UMG.TextWidgetBuilder.OverflowClippingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderOverflowClippingContractTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("overflow_clipping_contract");
	Node->Name = TEXT("Overflow Clipping Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("DATA ANALYTICS");
	Node->Text.FontFamily = TEXT("Poppins");
	Node->Text.FontSize = 64.0f;
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestFalse(TEXT("Fixed box without truncate stays unclipped (overflow is visible)"),
		Widget.Render.Clipping.IsSet());

	Node->Text.bTruncateOverflow = true;
	const FUMGWidgetNode TruncatingWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Truncate mode keeps clipping"),
		TruncatingWidget.Render.Clipping.Get(FString()), FString(TEXT("ClipToBounds")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderLineHeightBaselineShiftContractTest,
	"Design2UMG.TextWidgetBuilder.LineHeightBaselineShiftContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderLineHeightBaselineShiftContractTest::RunTest(const FString& Parameters)
{
	FFontResolver::ClearCache();
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!TestTrue(TEXT("Settings available"), Settings != nullptr))
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	Settings->ExplicitFontMappings.Empty();

	// Reuse the real project font asset: the baseline shift reads hhea metrics
	// from the resolved font face, which only authored (asset-backed) faces carry.
	UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Game/Design2UMG/Fonts/Poppins.Poppins"));
	if (!TestNotNull(TEXT("Baseline shift project font asset loaded"), Font))
	{
		Settings->ExplicitFontMappings = SavedMappings;
		return false;
	}
	Settings->ExplicitFontMappings.Add(
		TEXT("D2UBaselineShiftProbe"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(Font->GetPathName())));
	FFontResolver::ClearCache();

	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Id = TEXT("baseline_shift_contract");
	Node->Name = TEXT("Baseline Shift Contract");
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("DATA\nANALYTICS");
	Node->Text.FontFamily = TEXT("D2UBaselineShiftProbe");
	Node->Text.FontSize = 64.0f;
	Node->Text.LineHeight = 0.89f;
	// Set by the Figma parser for its half-leading first-line placement.
	Node->Text.bSourceHalfLeadingBaseline = true;
	Node->Text.TextGrowth = EDesignTextGrowth::FixedWidthHeight;

	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestTrue(TEXT("Multi-line explicit line height applies the percentage"),
		Widget.Text.LineHeightPercentage.IsSet());
	const bool bMultiLineShift = Widget.Text.LineHeightBaselineShift.IsSet();
	TestTrue(TEXT("Multi-line explicit line height applies the baseline shift"), bMultiLineShift);
	if (bMultiLineShift)
	{
		TestTrue(TEXT("Baseline shift is finite"),
			FMath::IsFinite(Widget.Text.LineHeightBaselineShift.GetValue()));
	}

	// The baseline difference is a placement fact of the first line, so the shift
	// also applies to single-line labels whose line advance stays at the natural
	// height (no LineHeightPercentage).
	Node->Text.Content = TEXT("HOME");
	const FUMGWidgetNode LabelWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestFalse(TEXT("Single-line label keeps natural line advance"),
		LabelWidget.Text.LineHeightPercentage.IsSet());
	TestEqual(TEXT("Single-line label still gets the baseline shift"),
		LabelWidget.Text.LineHeightBaselineShift.IsSet(), bMultiLineShift);

	// Pencil keeps the natural ascent at the top of its line boxes, which already
	// matches Slate, so its parser leaves the flag off and no shift is generated.
	Node->Text.bSourceHalfLeadingBaseline = false;
	const FUMGWidgetNode AscentTopWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestFalse(TEXT("Ascent-top sources get no baseline shift"),
		AscentTopWidget.Text.LineHeightBaselineShift.IsSet());

	// A fallback font (no resolvable source asset) breaks the metric basis of the
	// shift, so no shift is generated for it either.
	Node->Text.bSourceHalfLeadingBaseline = true;
	Node->Text.FontFamily = TEXT("D2UBaselineShiftUnmappedFamily");
	const FUMGWidgetNode FallbackWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestFalse(TEXT("Unresolvable font families get no baseline shift"),
		FallbackWidget.Text.LineHeightBaselineShift.IsSet());

	Settings->ExplicitFontMappings = SavedMappings;
	FFontResolver::ClearCache();
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

