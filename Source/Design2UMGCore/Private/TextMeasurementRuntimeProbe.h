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
	bool TryMeasureIntrinsicWidth(const FTextMeasureRequest& Request, float& OutWidth);
	bool TryResolveIconBaselineShift(const FTextMeasureRequest& Request, const float BoxHeight, float& OutShift);
	bool TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize);

	/** Release the module-owned probe text block before shutdown / hot-reload. */
	void ResetProbeForModuleShutdown();
}
