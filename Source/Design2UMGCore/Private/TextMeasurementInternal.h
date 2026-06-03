// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "TextMeasurement.h"

#include "HAL/CriticalSection.h"

struct FDesignNode;
struct FSlateFontInfo;

namespace RuntimeProbe
{
	extern const float FallbackMeasureWidthScale;
	extern const float UnresolvedFontFallbackMeasureWidthScale;
	extern const float FallbackMeasureHeightScale;

	FSlateFontInfo BuildSlateFontInfoForSize(const TSharedPtr<FDesignNode>& Node, float FontSize);
	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing = 0);
	float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize);
	FVector2D MeasureUMGTextBlockDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		float FontSize,
		const FString& Text,
		float WrapTextAt = 0.0f,
		bool bAutoWrapText = false);
	int32 ResolveMaxProbeFontSize(float DeclaredFontSize, float FallbackFontSize);
}

#if !defined(DESIGN2UMG_TEXTMEASUREMENT_CACHE_DECLARATIONS_ONLY)
namespace SharedThresholds
{
	inline constexpr float LargeNonWrappingFontThreshold = 24.0f;
	inline constexpr int32 CompactSingleLineLabelWordLimit = 2;
	inline constexpr float CompactSingleLineLabelWidthThreshold = 80.0f;
	inline constexpr float CompactSingleLineLabelWidthInset = 5.0f;
	inline constexpr float DefaultFigmaWidthInset = 4.0f;
}

namespace LineBox
{
	float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node);
}

namespace FallbackCalibration
{
	extern const float IntrinsicSingleLineSlackThreshold;

	float ResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node);
	float ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node);
	TOptional<float> TryResolveSemanticSingleLineFallbackScale(const TSharedPtr<FDesignNode>& Node);
	TOptional<float> TryResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node);
	TOptional<float> TryResolveMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node);
	float ResolveTextFontSizeFallback(const TSharedPtr<FDesignNode>& Node);
	FString NormalizeTextMeasurementFontFamilyKey(const FString& FontFamily);
	float FindFamilySpecificTextFallbackScale(const FString& NormalizedFamily);
	float FindFamilySpecificTextFallbackScale();
	bool ShouldUseDeclaredFontSizeDirectly(const TSharedPtr<FDesignNode>& Node);
	bool TryGetDefaultSlateTextFontAssetPath(FString& OutAssetPath);
	bool IsDefaultSlateTextFontAssetPath(const FString& AssetPath);
}

namespace FontCalibration
{
	FString ResolveTypefaceName(const FString& FontFamily, const FString& FontWeight);
	FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node);
}

namespace SharedThresholdOwners
{
	bool ShouldApplyLargeNonWrappingExplicitLineHeight(const TSharedPtr<FDesignNode>& Node);
}

namespace SingleLineWidthFit
{
	struct FProbePlan
	{
		float TargetTextWidth = 0.0f;
		float WidthFitTarget = 0.0f;
		int32 MaxProbeFontSize = 0;
		bool bAllowClosestOvershoot = false;
		bool bConstrainWrappedHeight = false;
	};

	bool IsCompactSingleLineLabel(const TSharedPtr<FDesignNode>& Node, float TargetTextWidth);
	bool TryBuildProbePlan(const TSharedPtr<FDesignNode>& Node, float FallbackFontSize, FProbePlan& OutPlan);
	int32 ResolveWrappedHeightConstrainedFontSize(
		const TSharedPtr<FDesignNode>& Node,
		const FProbePlan& Plan,
		TFunctionRef<float(float)> MeasureWidth);
}

namespace RuntimeProbeSizing
{
	int32 ResolveBestProbeFontSize(
		float FallbackFontSize,
		int32 MaxProbeFontSize,
		float TargetValue,
		TFunctionRef<float(float)> MeasureValue);
	int32 ResolveBestWidthFitFontSize(
		float FallbackFontSize,
		int32 MaxProbeFontSize,
		float TargetWidth,
		bool bAllowClosestOvershoot,
		TFunctionRef<float(float)> MeasureWidth);
	bool ShouldUseSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node, float UnwrappedWidth, float ParentContentWidth);
	int32 ResolveVerticalAlignedWrappingFontSize(
		const TSharedPtr<FDesignNode>& Node,
		float FallbackFontSize,
		float TargetLineBoxHeight);
	float ResolveTextFontSizeForUMGInternal(const TSharedPtr<FDesignNode>& Node, FString* OutResolutionPath);
}

namespace SingleLineHeuristics
{
	inline constexpr float WidthHeuristicScale = 0.55f;

	bool IsHeavyFontWeight(const FString& FontWeight);
	int32 CountSingleLineTextWords(const TSharedPtr<FDesignNode>& Node);
	bool IsDenseTechnicalTokenText(const TSharedPtr<FDesignNode>& Node);
	bool IsExpandedRefIntrinsicSingleLineText(const TSharedPtr<FDesignNode>& Node);
	bool ShouldUseTightExpandedRefIntrinsicScale(const TSharedPtr<FDesignNode>& Node);
	bool IsRoomyQuestionPromptText(const TSharedPtr<FDesignNode>& Node);
	bool ShouldAllowClosestOvershootSingleLineWidthFit(const TSharedPtr<FDesignNode>& Node);
	bool ShouldClampSingleLineWidthFitToFallback(const TSharedPtr<FDesignNode>& Node, float FallbackFontSize, int32 BestFontSize);
	bool ShouldUseMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node);
	bool ShouldPreferIntrinsicSingleLineFallbackFontSize(const TSharedPtr<FDesignNode>& Node);
}

namespace SourceMetadata
{
	bool IsFigmaTruncateTextNode(const TSharedPtr<FDesignNode>& Node);
	bool IsFigmaSourceNode(const TSharedPtr<FDesignNode>& Node);
}

namespace TextContentRules
{
	bool HasExplicitParagraphBreaks(const FString& Text);
	bool HasExplicitParagraphBreaks(const TSharedPtr<FDesignNode>& Node);
}
#endif

namespace CacheTrace
{
	struct FResolvedScaleCacheOwner
	{
		TMap<FString, float> ResolvedTextScaleByCalibrationKey;
		TMap<FString, float> SharedStyleMinScaleByStyleKey;
		TMap<FString, float> BaselineFallbackScaleByProbeKey;
		mutable FCriticalSection Mutex;

		bool TryGetResolvedTextScale(const FString& CalibrationKey, float& OutScale) const;
		void StoreResolvedTextScale(const FString& CalibrationKey, float Scale);
		bool TryGetSharedStyleMinScale(const FString& SharedKey, float& OutScale) const;
		void StoreSharedStyleMinScale(const FString& SharedKey, float Scale);
		bool TryGetBaselineFallbackScale(const FString& ProbeKey, float& OutScale) const;
		void StoreBaselineFallbackScale(const FString& ProbeKey, float Scale);
		void Reset();

#if WITH_AUTOMATION_WORKER
		TextMeasurement::Testing::FTextMeasurementCacheStats BuildStats() const;
#endif
	};

	extern FResolvedScaleCacheOwner GResolvedScaleCache;

	FString BuildBaselineFallbackScaleKey(const TSharedPtr<FDesignNode>& Node);
	bool TryGetResolvedTextFontScale(const FString& CalibrationKey, float& OutScale);
	void StoreResolvedTextFontScale(const FString& CalibrationKey, float Scale);
	bool TryGetResolvedSharedTextFontScaleMin(const FString& SharedKey, float& OutScale);
	void StoreResolvedSharedTextFontScaleMin(const FString& SharedKey, float Scale);
	bool TryGetBaselineFallbackScale(const FString& ProbeKey, float& OutScale);
	void StoreBaselineFallbackScale(const FString& ProbeKey, float Scale);
	FString SummarizeTextForLog(const FString& Text);
	void LogResolvedTextDecision(
		const TSharedPtr<FDesignNode>& Node,
		const TCHAR* Branch,
		float ResolvedFontSize,
		float FallbackFontSize,
		FString* OutResolutionPath);
	FString BuildTextFontCalibrationKey(const TSharedPtr<FDesignNode>& Node);
	FString BuildTextFontSharedStyleKey(const TSharedPtr<FDesignNode>& Node);
}
