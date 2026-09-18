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
	TestTrue(TEXT("Style rows remain typed"), Widget.Text.RichTextStyleRows.IsSet());
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
	// from the resolved font face, independent of its storage location.
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
	// Set by parsers whose source paragraph renderer uses half-leading placement.
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
		TextMeasurementRuntimeProbe::FTextMeasureRequest FontMetrics;
		FontMetrics.Text = TEXT("DATA ANALYTICS");
		FontMetrics.FontSize = 48.0f;
		FontMetrics.FontAssetPath = Font->GetPathName();
		FVector2D NaturalFontSize;
		float LineGapEm = 0.0f;
		if (TestTrue(TEXT("Natural font metrics are available"),
			TextMeasurementRuntimeProbe::TryMeasureText(FontMetrics, NaturalFontSize))
			&& TestTrue(TEXT("Font line gap is available"),
				TextMeasurementRuntimeProbe::TryReadLineGapEm(TextMeasurementRuntimeProbe::BuildFontInfo(FontMetrics), LineGapEm)))
		{
			const float ExpectedShift = -((NaturalFontSize.Y - Node->Text.FontSize * Node->Text.LineHeight)
				+ LineGapEm * Node->Text.FontSize) / 2.0f;
			TestTrue(TEXT("Baseline alignment uses font metrics, not invisible widget shadow extents"),
				FMath::IsNearlyEqual(Widget.Text.LineHeightBaselineShift.GetValue(), ExpectedShift, 0.001f));
		}
	}

	// Explicit source line height applies to a single-line label as well.
	Node->Text.Content = TEXT("HOME");
	const FUMGWidgetNode LabelWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestTrue(TEXT("Single-line label keeps explicit source line height"),
		LabelWidget.Text.LineHeightPercentage.IsSet());
	TestEqual(TEXT("Single-line label still gets the baseline shift"),
		LabelWidget.Text.LineHeightBaselineShift.IsSet(), bMultiLineShift);

	// A source using natural ascent-top placement needs no baseline translation.
	Node->Text.bSourceHalfLeadingBaseline = false;
	const FUMGWidgetNode AscentTopWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestFalse(TEXT("Ascent-top sources get no baseline shift"),
		AscentTopWidget.Text.LineHeightBaselineShift.IsSet());

	// Measure the actual rendered face even when it is supplied by Slate rather
	// than a project font asset. This does not change which font is selected.
	Node->Text.bSourceHalfLeadingBaseline = true;
	Node->Text.FontFamily = TEXT("D2UBaselineShiftUnmappedFamily");
	const FUMGWidgetNode FallbackWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestTrue(TEXT("Default Slate face retains half-leading alignment"), FallbackWidget.Text.LineHeightBaselineShift.IsSet());
	Node->Text.FontFamily = TEXT("Roboto");
	const FUMGWidgetNode EngineFontWidget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestTrue(TEXT("Engine fonts retain half-leading alignment"), EngineFontWidget.Text.LineHeightBaselineShift.IsSet());
	TextMeasurementRuntimeProbe::FTextMeasureRequest EngineMetrics;
	EngineMetrics.Text = Node->Text.Content;
	EngineMetrics.FontSize = 48.0f;
	EngineMetrics.FontAssetPath = EngineFontWidget.Text.FontFamily.Get(FString());
	EngineMetrics.TypefaceName = EngineFontWidget.Text.TypefaceFontName.Get(FString());
	FVector2D EngineNaturalSize;
	float EngineLineGapEm = 0.0f;
	if (TestTrue(TEXT("Resolved engine font metrics readable"), TextMeasurementRuntimeProbe::TryMeasureText(EngineMetrics, EngineNaturalSize))
		&& TestTrue(TEXT("Engine font line gap readable"), TextMeasurementRuntimeProbe::TryReadLineGapEm(TextMeasurementRuntimeProbe::BuildFontInfo(EngineMetrics), EngineLineGapEm)))
	{
		const float ExpectedShift = -((EngineNaturalSize.Y - Node->Text.FontSize * Node->Text.LineHeight)
			+ EngineLineGapEm * Node->Text.FontSize) / 2.0f;
		TestTrue(TEXT("Engine font shift uses the same metric rule as project fonts"),
			EngineFontWidget.Text.LineHeightBaselineShift.IsSet()
			&& FMath::IsNearlyEqual(EngineFontWidget.Text.LineHeightBaselineShift.GetValue(), ExpectedShift, 0.001f));
	}

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
	if (!TestTrue(TEXT("Rich text style rows are present"), Widget.Text.RichTextStyleRows.IsSet()))
	{
		return false;
	}

	const auto& Rows = Widget.Text.RichTextStyleRows.GetValue();
	const auto* ValidRow = Rows.Find(TEXT("d2u_run_0"));
	const auto* InvalidRow = Rows.Find(TEXT("d2u_run_1"));
	if (!TestNotNull(TEXT("Valid run row exists"), ValidRow) || !TestNotNull(TEXT("Invalid run row exists"), InvalidRow)) return false;
	TestTrue(TEXT("Valid run has an applied point size"), ValidRow->FontSize.IsSet());
	TestEqual(TEXT("16 source pixels convert to 12 points independently of the invalid parent size"), ValidRow->FontSize.Get(0.0f), 12.0f);
	TestFalse(TEXT("Invalid explicit run size does not inherit or bypass conversion"), InvalidRow->FontSize.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextWidgetBuilderMultilineMarkupTest,
	"Design2UMG.TextWidgetBuilder.MultilineMarkup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextWidgetBuilderMultilineMarkupTest::RunTest(const FString&)
{
	const TSharedPtr<FDesignNode> Node = MakeFixedNode(EDesignNodeType::Text, 200.0f, 120.0f);
	Node->Text.FontSize = 16.0f;
	Node->Text.Content = TEXT("A&B\r\n\r\n<C>");
	FDesignTextRun& Run = Node->Text.TextRuns.AddDefaulted_GetRef();
	Run.Content = Node->Text.Content;
	Run.bUnderline = true;
	const FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
	TestEqual(TEXT("Each nonempty line has balanced tags; blank lines remain blank"),
		Widget.Text.Text.Get(FString()),
		FString(TEXT("<d2u_run_0>A&amp;B</>\r\n\r\n<d2u_run_0>&lt;C&gt;</>")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

