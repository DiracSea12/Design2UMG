// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "TextMeasurement.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleLineSourceMetricsTest,
	"Design2UMG.TextMeasurement.SingleLineSourceMetrics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSingleLineSourceMetricsTest::RunTest(const FString&)
{
	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
	Node->Type = EDesignNodeType::Text;
	Node->Text.Content = TEXT("MEASURE");
	Node->Text.FontSize = 32.0f;
	Node->Text.TextGrowth = EDesignTextGrowth::Auto;
	Node->Transform.Width = FDesignSize::Fit(0.0f);
	Node->Transform.Height = FDesignSize::Fit(0.0f);
	Node->Text.SetLetterSpacingValue(0.0f);
	const FVector2D Plain = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
	Node->Text.SetLetterSpacingValue(3.0f);
	const FVector2D Spaced = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
	TestTrue(TEXT("Single-line size measures the tracking used by the rendered text"), Spaced.X > Plain.X + 10.0f);
	for (const float Ratio : {0.8f, 1.0f, 1.5f})
	{
		Node->Text.LineHeight = Ratio;
		const FVector2D Size = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
		AddInfo(FString::Printf(TEXT("Source line height %.3f, measured %.3f"), Node->Text.FontSize * Ratio, Size.Y));
		TestTrue(FString::Printf(TEXT("Explicit single-line height %.1f is respected"), Ratio),
			FMath::IsNearlyEqual(Size.Y, static_cast<double>(Node->Text.FontSize * Ratio), 0.05));
	}
	Node->Text.bSourceRoundLineHeightToPixels = true;
	Node->Text.FontSize = 13.0f;
	Node->Text.LineHeight = 1.4f;
	TestTrue(TEXT("Source pixel-rounded line height measures 18 rather than 18.2"),
		FMath::IsNearlyEqual(TextMeasurement::MeasureTextNodeSize(Node, 0.0f).Y, 18.0, 0.001));
	Node->Text.FontSize = 39.0f;
	TestTrue(TEXT("Rounding follows the resolved instance font size, not a baked definition ratio"),
		FMath::IsNearlyEqual(TextMeasurement::MeasureTextNodeSize(Node, 0.0f).Y, 55.0, 0.001));
	return true;
}
#endif
