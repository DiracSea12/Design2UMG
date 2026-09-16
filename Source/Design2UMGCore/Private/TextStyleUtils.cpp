// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextStyleUtils.h"

int32 TextStyleUtils::ConvertLetterSpacingPxToTracking(
	const float PixelSpacing,
	const float AppliedFontSize)
{
	if (!FMath::IsFinite(AppliedFontSize) || AppliedFontSize <= 0.0f || !FMath::IsFinite(PixelSpacing))
	{
		return 0;
	}

	// Slate multiplies tracking by the applied point size, not the source pixel size,
	// then truncates to integer pixels. Round away from zero so an exact requested
	// pixel gap does not fall below that integer boundary (e.g. 333 * 9 / 1000).
	const double Tracking = static_cast<double>(PixelSpacing) * 1000.0 / AppliedFontSize;
	return Tracking < 0.0 ? FMath::FloorToInt(Tracking) : FMath::CeilToInt(Tracking);
}
