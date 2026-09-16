// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "TextStyleUtils.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextTrackingPixelUnitsTest,
	"Design2UMG.TextMeasurement.TrackingPixelUnits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTextTrackingPixelUnitsTest::RunTest(const FString&)
{
	// Slate shaping truncates tracking * the applied point size / 1000 to integer pixels.
	for (const float AppliedSize : {7.5f, 9.0f, 12.0f, 18.0f, 99.0f})
	{
		for (const float Spacing : {-8.0f, -3.0f, 0.0f, 1.0f, 3.0f, 8.0f})
		{
			const int32 Tracking = TextStyleUtils::ConvertLetterSpacingPxToTracking(Spacing, AppliedSize);
			const int32 ShapedPixelSpacing = static_cast<int32>(Tracking * AppliedSize / 1000.0f);
			TestEqual(FString::Printf(TEXT("Spacing %.1f survives point-size %.1f and Slate truncation"), Spacing, AppliedSize),
				ShapedPixelSpacing, static_cast<int32>(Spacing));
		}
	}
	return true;
}
#endif
