// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace TextStyleUtils
{
	inline int32 ConvertLetterSpacingPxToTracking(float PixelSpacing, float DesignFontSize, float AppliedFontSize)
	{
		const float BaseFontSize = DesignFontSize > 0.0f ? DesignFontSize : AppliedFontSize;
		if (BaseFontSize <= 0.0f)
		{
			return 0;
		}

		return FMath::RoundToInt(PixelSpacing / BaseFontSize * 1000.0f);
	}
}
