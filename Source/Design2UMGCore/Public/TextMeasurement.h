// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

/** Text fitting and measurement facade used by converter text paths. */
namespace TextMeasurement
{
	/** Low-level primitive for clearing process-local text measurement caches. Shared callers should prefer the named boundary resets below. */
	DESIGN2UMGCORE_API void ResetCache();
	/** Preferred shared-path reset for starting an explicit document conversion boundary. */
	DESIGN2UMGCORE_API void ResetCacheForConversionBoundary();
	/** Preferred shared-path reset for starting an explicit automation render boundary. */
	DESIGN2UMGCORE_API void ResetCacheForAutomationBoundary();
	/** Return whether the node should use wrapping text behavior. */
	DESIGN2UMGCORE_API bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	/** Decide whether explicit line height should be applied for the node. */
	DESIGN2UMGCORE_API bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f);
	/** Resolve the UMG runtime font size for a design text node. */
	DESIGN2UMGCORE_API float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	/** Resolve the UMG wrap width for a design text node. */
	DESIGN2UMGCORE_API float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Return whether AutoWrapText should be enabled. */
	DESIGN2UMGCORE_API bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	/** Estimate runtime text size for layout conversion. */
	DESIGN2UMGCORE_API FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Normalize source text for runtime UMG display. */
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
		enum class ETextFontSizeFallbackBranch : uint8
		{
			None,
			Measured,
			Semantic,
			FamilyOrDefault
		};

		struct FSlateMetricFingerprintTrace
		{
			float SignedBaselineRatio = 0.0f;
			float MaxHeightRatio = 0.0f;
			bool bIsAvailable = false;
			FString Token;
		};

		struct FTextFontSizeFallbackTrace
		{
			ETextFontSizeFallbackBranch Branch = ETextFontSizeFallbackBranch::None;
			float Scale = 0.0f;
		};

		DESIGN2UMGCORE_API float ResolveSingleLineWidthFitTarget(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API FSlateMetricFingerprintTrace MeasureSlateMetricFingerprint(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API FString BuildBaselineFallbackScaleKeyForTest(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveTextFontSizeFallbackForTest(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API FTextFontSizeFallbackTrace ResolveTextFontSizeFallbackTraceForTest(const TSharedPtr<FDesignNode>& Node);
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
