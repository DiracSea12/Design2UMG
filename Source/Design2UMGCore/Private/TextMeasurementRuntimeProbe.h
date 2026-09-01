// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

struct FSlateFontInfo;

namespace TextMeasurementRuntimeProbe
{
	struct FTextMeasureRequest
	{
		FString Text;
		float FontSize = 0.0f;
		FString FontAssetPath;
		FString TypefaceName;
		int32 LetterSpacing = 0;
		bool bRequiresResolvedFontAsset = false;
	};

	struct FTextBlockMeasureRequest : FTextMeasureRequest
	{
		EDesignTextAlign TextAlign = EDesignTextAlign::Left;
		bool bApplyExplicitLineHeight = false;
		float LineHeightPercentage = 1.0f;
		bool bAutoWrapText = false;
		float WrapTextAt = 0.0f;
	};

	bool IsAvailable();
	FSlateFontInfo BuildFontInfo(const FTextMeasureRequest& Request);
	bool TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize);
	bool TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize);

	/**
	 * Reads the font's hhea lineGap in em units from the resolved font asset's raw TTF
	 * data (face picked by TypefaceName using the same rule as the Slate composite font
	 * cache: exact typeface-entry match, else the primary entry). Returns false when the
	 * font asset or its TTF bytes cannot be loaded.
	 */
	bool TryReadLineGapEm(const FString& FontAssetPath, const FString& TypefaceName, float& OutLineGapEm);
}
