// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

namespace TextMeasurement
{
	DESIGN2UMGCORE_API void ResetCache();
	DESIGN2UMGCORE_API bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f);
	DESIGN2UMGCORE_API float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	DESIGN2UMGCORE_API bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	DESIGN2UMGCORE_API FString PrepareRuntimeTextContent(const FString& Text);

#if WITH_AUTOMATION_WORKER
	namespace Testing
	{
		struct FTextMeasurementCacheStats
		{
			int32 ResolvedScaleEntries = 0;
			int32 SharedStyleScaleEntries = 0;
			int32 BaselineFallbackEntries = 0;
		};

		// Tests-only read-only bridge for Batch B executable evidence.
		DESIGN2UMGCORE_API FTextMeasurementCacheStats GetCacheStats();
		DESIGN2UMGCORE_API float EstimateSingleLineHeight(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveBaselineFallbackScale(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveSingleLineWidthFitTarget(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API int32 ResolveBestWidthFitFontSizeFromWidths(
			float FallbackFontSize,
			int32 MaxProbeFontSize,
			float TargetWidth,
			bool bAllowClosestOvershoot,
			const TArray<float>& CandidateWidths);
		DESIGN2UMGCORE_API FVector2D MeasureFallbackTextSize(const FString& Text, float FontSize);
		DESIGN2UMGCORE_API float EstimateSingleLineHeuristicWidth(const FString& Text, float FontSize);
	}
#endif
}
