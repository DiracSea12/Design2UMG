// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "UMGPropertyApplierInternal.h"
#include "UMGPropertyApplierSupport.h"
#include "UMGSchema.h"
#include "Components/Button.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	void ExpectColorEquals(FAutomationTestBase& Test, const TCHAR* What,
		const FLinearColor& Actual, const FLinearColor& Expected)
	{
		const bool bMatches = Actual.Equals(Expected, 1e-6f);
		Test.TestTrue(FString::Printf(TEXT("%s: expected (%f,%f,%f,%f) got (%f,%f,%f,%f)"),
			What,
			Expected.R, Expected.G, Expected.B, Expected.A,
			Actual.R, Actual.G, Actual.B, Actual.A), bMatches);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FButtonStateTintColorBoundaryTest,
	"Design2UMG.ButtonState.TintColorBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FButtonStateTintColorBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace UMGPropertyApplierSupport;

	// Hover brightens toward white, so even black has a visible RGB state color.
	// Pressed darkens toward black. Both derived colors stay in the displayable
	// RGB range while preserving the authored alpha channel exactly.
	ExpectColorEquals(*this, TEXT("Black hover"), MakeHoverTintColor(FLinearColor::Black), FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
	ExpectColorEquals(*this, TEXT("Black pressed"), MakePressedTintColor(FLinearColor::Black), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));

	ExpectColorEquals(*this, TEXT("White hover"), MakeHoverTintColor(FLinearColor::White), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	ExpectColorEquals(*this, TEXT("White pressed"), MakePressedTintColor(FLinearColor::White), FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));

	// Mid gray is still a 10% state change in RGB, with alpha untouched.
	const FLinearColor Gray(0.5f, 0.5f, 0.5f, 1.0f);
	ExpectColorEquals(*this, TEXT("Gray hover"), MakeHoverTintColor(Gray), FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
	ExpectColorEquals(*this, TEXT("Gray pressed"), MakePressedTintColor(Gray), FLinearColor(0.45f, 0.45f, 0.45f, 1.0f));

	// Alpha is independent of the state tint, including transparent input.
	const FLinearColor AlphaProbe(0.2f, 0.4f, 0.6f, 0.23f);
	const FLinearColor HoverAlphaProbe = MakeHoverTintColor(AlphaProbe);
	const FLinearColor PressedAlphaProbe = MakePressedTintColor(AlphaProbe);
	TestTrue(TEXT("Hover preserves authored alpha"), FMath::IsNearlyEqual(HoverAlphaProbe.A, AlphaProbe.A));
	TestTrue(TEXT("Pressed preserves authored alpha"), FMath::IsNearlyEqual(PressedAlphaProbe.A, AlphaProbe.A));

	// Out-of-range authored RGB is normalized to a usable state tint; alpha stays
	// untouched because it is an authored opacity, not part of the tint operation.
	const FLinearColor Overflow(1.5f, -0.5f, 0.5f, 0.61f);
	const FLinearColor HoverOverflow = MakeHoverTintColor(Overflow);
	const FLinearColor PressedOverflow = MakePressedTintColor(Overflow);
	const auto IsRgbInRange = [](const FLinearColor& Color)
	{
		return Color.R >= 0.0f && Color.R <= 1.0f
			&& Color.G >= 0.0f && Color.G <= 1.0f
			&& Color.B >= 0.0f && Color.B <= 1.0f;
	};
	TestTrue(TEXT("Hover clamps RGB overflow"), IsRgbInRange(HoverOverflow));
	TestTrue(TEXT("Pressed clamps RGB overflow"), IsRgbInRange(PressedOverflow));
	TestTrue(TEXT("Hover keeps overflow alpha"), FMath::IsNearlyEqual(HoverOverflow.A, Overflow.A));
	TestTrue(TEXT("Pressed keeps overflow alpha"), FMath::IsNearlyEqual(PressedOverflow.A, Overflow.A));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FButtonStateTintExplicitColorPrecedenceTest,
	"Design2UMG.ButtonState.ExplicitColorPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FButtonStateTintExplicitColorPrecedenceTest::RunTest(const FString& Parameters)
{
	UButton* Button = NewObject<UButton>();
	FUMGWidgetNode Node;
	Node.Type = TEXT("Button");
	Node.WidgetStyle.BackgroundColor = FLinearColor::Black;
	Node.WidgetStyle.HoveredColor = FLinearColor(0.2f, 0.4f, 0.6f, 0.35f);
	Node.WidgetStyle.PressedColor = FLinearColor(0.7f, 0.5f, 0.3f, 0.65f);

	UMGPropertyApplierInternal::ApplyWidgetProperties(Button, Node);
	const FButtonStyle& Style = Button->GetStyle();
	TestTrue(TEXT("Explicit hovered color overrides derived tint"),
		Style.Hovered.TintColor.GetSpecifiedColor().Equals(Node.WidgetStyle.HoveredColor.GetValue()));
	TestTrue(TEXT("Explicit pressed color overrides derived tint"),
		Style.Pressed.TintColor.GetSpecifiedColor().Equals(Node.WidgetStyle.PressedColor.GetValue()));
	return true;
}

#endif
