// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

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

	struct FSlateTextMetricFingerprint
	{
		float SignedBaselineRatio = 0.0f;
		float MaxHeightRatio = 0.0f;
		bool bIsAvailable = false;
	};

	bool IsAvailable();
	FSlateTextMetricFingerprint MeasureSlateTextMetricFingerprint(const FTextMeasureRequest& Request);
	FString SerializeSlateTextMetricToken(const FSlateTextMetricFingerprint& Metric);
	bool TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize);
	bool TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize);
}
