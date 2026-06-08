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

		struct FSlateMetricProbeResult
		{
			float SignedBaselineRatio = 0.0f;
			float MaxHeightRatio = 0.0f;
			double ElapsedSeconds = 0.0;
			FString Token;
			bool bAvailable = false;
		};

		struct FProtectedMeasuredCandidateResult
		{
			float StableAcceptedScale = 0.0f;
			float CandidateScale = 0.0f;
			int32 MeasurementAttempts = 0;
			bool bCandidateAvailable = false;
			bool bBlockedByMissingFontIdentity = false;
			bool bBlockedByFallbackState = false;
			FString FallbackState;
			FString MetricToken;
		};

		// Tests-only read-only bridge for Batch B executable evidence.
		DESIGN2UMGCORE_API FTextMeasurementCacheStats GetCacheStats();
		DESIGN2UMGCORE_API float EstimateSingleLineHeight(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveBaselineFallbackScale(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API float ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API FString BuildBaselineFallbackScaleKey(const TSharedPtr<FDesignNode>& Node);
		DESIGN2UMGCORE_API FString BuildSlateMetricToken(float SignedBaselineRatio, float MaxHeightRatio, bool bAvailable);
		DESIGN2UMGCORE_API FSlateMetricProbeResult MeasureSlateMetricProbe(const TSharedPtr<FDesignNode>& Node, int32 BatchCount);
		DESIGN2UMGCORE_API FProtectedMeasuredCandidateResult ResolveProtectedMeasuredCandidate(const TSharedPtr<FDesignNode>& Node);
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
