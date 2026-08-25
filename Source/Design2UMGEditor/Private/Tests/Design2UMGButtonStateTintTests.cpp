// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
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

	// Black hover/pressed: RGB stays black; alpha scales with the multiplier
	// (engine operator* scales RGBA) - locked so any change is deliberate.
	ExpectColorEquals(*this, TEXT("Black hover"), MakeHoverTintColor(FLinearColor::Black), FLinearColor(0.0f, 0.0f, 0.0f, 1.1f));
	ExpectColorEquals(*this, TEXT("Black pressed"), MakePressedTintColor(FLinearColor::Black), FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));

	// White hover exceeds 1 (no clamp by design; HDR-safe), pressed darkens.
	// FLinearColor::operator* scales RGBA including alpha.
	ExpectColorEquals(*this, TEXT("White hover"), MakeHoverTintColor(FLinearColor::White), FLinearColor(1.1f, 1.1f, 1.1f, 1.1f));
	ExpectColorEquals(*this, TEXT("White pressed"), MakePressedTintColor(FLinearColor::White), FLinearColor(0.9f, 0.9f, 0.9f, 0.9f));

	// Mid gray scales proportionally in RGBA.
	const FLinearColor Gray(0.5f, 0.5f, 0.5f, 1.0f);
	ExpectColorEquals(*this, TEXT("Gray hover"), MakeHoverTintColor(Gray), FLinearColor(0.55f, 0.55f, 0.55f, 1.1f));
	ExpectColorEquals(*this, TEXT("Gray pressed"), MakePressedTintColor(Gray), FLinearColor(0.45f, 0.45f, 0.45f, 0.9f));

	// Transparent stays transparent.
	ExpectColorEquals(*this, TEXT("Transparent hover"), MakeHoverTintColor(FLinearColor::Transparent), FLinearColor::Transparent);
	ExpectColorEquals(*this, TEXT("Transparent pressed"), MakePressedTintColor(FLinearColor::Transparent), FLinearColor::Transparent);

	return true;
}

#endif
