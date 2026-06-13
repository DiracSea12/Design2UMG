// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "FontResolver.h"
#include "SharedCoreSemantics.h"
#include "TextMeasurementRuntimeProbe.h"

#include "Design2UMGLog.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
	}
}

namespace
{
namespace WrapHeight
{
	constexpr float MinWrapProbeScale = 0.70f;

	int32 EstimateWrappedLineCount(float SingleLineHeight, const FVector2D& WrappedSize)
	{
		if (SingleLineHeight <= 0.0f || WrappedSize.Y <= 0.0f)
		{
			return 0;
		}

		return FMath::Max(1, FMath::RoundToInt(WrappedSize.Y / FMath::Max(1.0f, SingleLineHeight)));
	}
}

namespace SharedCalibration
{
	bool ShouldParticipateInSharedTextStyleCalibration(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || SourceMetadata::IsFigmaTruncateTextNode(Node))
		{
			return false;
		}

		if (Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top)
		{
			return false;
		}

		return Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->Text.TextAlign == EDesignTextAlign::Justify;
	}

	bool ShouldConsumeSharedTextStyleCalibration(const TSharedPtr<FDesignNode>& Node)
	{
		return ShouldParticipateInSharedTextStyleCalibration(Node)
			&& Node->Text.Content.Contains(TEXT("\n"));
	}
}
}

namespace RuntimeProbeSizing
{
namespace
{
	constexpr float WidthFitTolerance = 0.5f;
	constexpr float WidthFitOvershootTolerance = 2.0f;

	bool HasPositiveSourceWidth(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Transform.bSourceHasWidth
			&& (Node->Transform.Width.IsFixed() || Node->Transform.Width.IsFit())
			&& FMath::IsFinite(Node->Transform.Width.Value)
			&& Node->Transform.Width.Value > 0.0f;
	}

	bool HasPositiveSourceHeight(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Transform.bSourceHasHeight
			&& (Node->Transform.Height.IsFixed() || Node->Transform.Height.IsFit())
			&& FMath::IsFinite(Node->Transform.Height.Value)
			&& Node->Transform.Height.Value > 0.0f;
	}

	int32 ResolveSourceWidthSingleLineGrowthCap(
		const TSharedPtr<FDesignNode>& Node,
		const float FallbackFontSize,
		const int32 BestFontSize)
	{
		if (!Node.IsValid()
			|| !FMath::IsFinite(FallbackFontSize)
			|| FallbackFontSize <= 0.0f
			|| !HasPositiveSourceWidth(Node)
			|| Node->Text.Content.Contains(TEXT("\n"))
			|| TextMeasurement::ShouldAutoWrapText(Node)
			|| Node->Text.TextGrowth != EDesignTextGrowth::Auto)
		{
			return INDEX_NONE;
		}

		const int32 FallbackNearFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
		if (BestFontSize <= FallbackNearFontSize)
		{
			return INDEX_NONE;
		}

		return FallbackNearFontSize;
	}

	int32 ResolveFixedHeightSingleLineWidthFitCap(
		const TSharedPtr<FDesignNode>& Node,
		const int32 BestFontSize)
	{
		if (!Node.IsValid()
			|| BestFontSize <= 1
			|| Node->Text.Content.Contains(TEXT("\n"))
			|| TextMeasurement::ShouldAutoWrapText(Node)
			|| Node->Text.TextGrowth != EDesignTextGrowth::Auto
			|| !HasPositiveSourceHeight(Node)
			|| !HasPositiveSourceWidth(Node))
		{
			return INDEX_NONE;
		}

		for (int32 CandidateFontSize = BestFontSize; CandidateFontSize >= 1; --CandidateFontSize)
		{
			const float CandidateHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, static_cast<float>(CandidateFontSize));
			if (CandidateHeight > 0.0f && CandidateHeight <= Node->Transform.Height.Value + 0.5f)
			{
				return CandidateFontSize;
			}
		}

		return INDEX_NONE;
	}
}

int32 ResolveBestProbeFontSize(
	float FallbackFontSize,
	int32 MaxProbeFontSize,
	float TargetValue,
	TFunctionRef<float(float)> MeasureValue)
{
	int32 BestFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
	float BestError = TNumericLimits<float>::Max();

	for (int32 CandidateFontSize = 1; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
	{
		const float CandidateValue = MeasureValue(static_cast<float>(CandidateFontSize));
		if (CandidateValue <= 0.0f)
		{
			continue;
		}

		const float Error = FMath::Abs(CandidateValue - TargetValue);
		const float DistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
		const float BestDistanceToFallback = FMath::Abs(static_cast<float>(BestFontSize) - FallbackFontSize);
		if (Error + KINDA_SMALL_NUMBER < BestError
			|| (FMath::IsNearlyEqual(Error, BestError, 0.01f) && DistanceToFallback < BestDistanceToFallback))
		{
			BestFontSize = CandidateFontSize;
			BestError = Error;
		}
	}

	return BestError == TNumericLimits<float>::Max() ? INDEX_NONE : BestFontSize;
}

int32 ResolveBestWidthFitFontSize(
	float FallbackFontSize,
	int32 MaxProbeFontSize,
	float TargetWidth,
	bool bAllowClosestOvershoot,
	TFunctionRef<float(float)> MeasureWidth)
{
	int32 BestFittingFontSize = INDEX_NONE;
	float BestRemainingWidth = TNumericLimits<float>::Max();
	int32 BestOvershootFontSize = INDEX_NONE;
	float BestOvershootAmount = TNumericLimits<float>::Max();
	int32 ClosestFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
	float ClosestError = TNumericLimits<float>::Max();
	float ClosestWidth = 0.0f;

	for (int32 CandidateFontSize = 1; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
	{
		const float CandidateWidth = MeasureWidth(static_cast<float>(CandidateFontSize));
		if (CandidateWidth <= 0.0f)
		{
			continue;
		}

		const float Error = FMath::Abs(CandidateWidth - TargetWidth);
		const float DistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
		const float BestDistanceToFallback = FMath::Abs(static_cast<float>(ClosestFontSize) - FallbackFontSize);
		if (Error + KINDA_SMALL_NUMBER < ClosestError
			|| (FMath::IsNearlyEqual(Error, ClosestError, 0.01f) && DistanceToFallback < BestDistanceToFallback))
		{
			ClosestFontSize = CandidateFontSize;
			ClosestError = Error;
			ClosestWidth = CandidateWidth;
		}

		if (CandidateWidth <= TargetWidth + WidthFitTolerance)
		{
			const float RemainingWidth = TargetWidth - CandidateWidth;
			if (BestFittingFontSize == INDEX_NONE
				|| RemainingWidth + KINDA_SMALL_NUMBER < BestRemainingWidth
				|| (FMath::IsNearlyEqual(RemainingWidth, BestRemainingWidth, 0.01f) && CandidateFontSize > BestFittingFontSize))
			{
				BestFittingFontSize = CandidateFontSize;
				BestRemainingWidth = RemainingWidth;
			}
		}
		else if (bAllowClosestOvershoot && CandidateWidth <= TargetWidth + WidthFitOvershootTolerance)
		{
			const float OvershootAmount = CandidateWidth - TargetWidth;
			const float DistanceToFallbackForOvershoot = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
			const float BestOvershootDistanceToFallback =
				BestOvershootFontSize == INDEX_NONE
				? TNumericLimits<float>::Max()
				: FMath::Abs(static_cast<float>(BestOvershootFontSize) - FallbackFontSize);
			if (BestOvershootFontSize == INDEX_NONE
				|| OvershootAmount + KINDA_SMALL_NUMBER < BestOvershootAmount
				|| (FMath::IsNearlyEqual(OvershootAmount, BestOvershootAmount, 0.01f)
					&& DistanceToFallbackForOvershoot + KINDA_SMALL_NUMBER < BestOvershootDistanceToFallback)
				|| (FMath::IsNearlyEqual(OvershootAmount, BestOvershootAmount, 0.01f)
					&& FMath::IsNearlyEqual(DistanceToFallbackForOvershoot, BestOvershootDistanceToFallback, 0.01f)
					&& CandidateFontSize > BestOvershootFontSize))
			{
				BestOvershootFontSize = CandidateFontSize;
				BestOvershootAmount = OvershootAmount;
			}
		}
	}

	if (bAllowClosestOvershoot && BestOvershootFontSize != INDEX_NONE)
	{
		return BestOvershootFontSize;
	}

	if (bAllowClosestOvershoot
		&& ClosestError != TNumericLimits<float>::Max()
		&& ClosestWidth > 0.0f
		&& ClosestWidth > TargetWidth + WidthFitTolerance
		&& ClosestWidth <= TargetWidth + WidthFitOvershootTolerance)
	{
		return ClosestFontSize;
	}

	if (BestFittingFontSize != INDEX_NONE)
	{
		return BestFittingFontSize;
	}

	return ClosestError == TNumericLimits<float>::Max() ? INDEX_NONE : ClosestFontSize;
}

bool ShouldUseSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node, float UnwrappedWidth, float ParentContentWidth)
{
	if (!Node.IsValid() || Node->Text.Content.Contains(TEXT("\n")))
	{
		return false;
	}

	if (!TextMeasurement::IsWrappingTextMode(Node))
	{
		return true;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, ParentContentWidth);
	if (WidthLimit <= 0.0f)
	{
		return false;
	}

	const float AppliedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	const float HeuristicFontSize = AppliedFontSize > 0.0f ? AppliedFontSize : Node->Text.FontSize;
	const float HeuristicWidth = Node->Text.Content.Len() * HeuristicFontSize * SingleLineHeuristics::WidthHeuristicScale;
	const float EffectiveWidth = UnwrappedWidth > 0.0f ? UnwrappedWidth : HeuristicWidth;
	return EffectiveWidth <= WidthLimit + 0.5f;
}

int32 ResolveVerticalAlignedWrappingFontSize(
	const TSharedPtr<FDesignNode>& Node,
	float FallbackFontSize,
	float TargetLineBoxHeight)
{
	constexpr float VerticalWrapSafetySlack = 1.0f;

	if (!Node.IsValid()
		|| Node->Text.Content.Contains(TEXT("\n"))
		|| Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top)
	{
		return INDEX_NONE;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, 0.0f);
	if (WidthLimit <= 0.0f)
	{
		return INDEX_NONE;
	}

	const int32 MaxProbeFontSize = FMath::Max(
		1,
		FMath::CeilToInt(FMath::Max(FallbackFontSize, Node->Text.FontSize)));
	const int32 MinProbeFontSize = FMath::Min(
		MaxProbeFontSize,
		FMath::Max(1, FMath::FloorToInt(FallbackFontSize * WrapHeight::MinWrapProbeScale)));
	const bool bAutoWrapText = TextMeasurement::ShouldAutoWrapText(Node);
	const float AvailableHeight = Node->Transform.Height.Value;
	const bool bHasFixedHeight = AvailableHeight > 0.0f;

	int32 BestFontSize = INDEX_NONE;
	int32 BestLineCount = MAX_int32;
	float BestWidthSlack = TNumericLimits<float>::Max();
	float BestHeightError = TNumericLimits<float>::Max();
	float BestDistanceToFallback = TNumericLimits<float>::Max();

	int32 BestSafeFontSize = INDEX_NONE;
	int32 BestSafeLineCount = MAX_int32;
	float BestSafeHeightSlack = TNumericLimits<float>::Max();
	float BestSafeWidthSlack = TNumericLimits<float>::Max();
	float BestSafeHeightError = TNumericLimits<float>::Max();
	float BestSafeDistanceToFallback = TNumericLimits<float>::Max();

	for (int32 CandidateFontSize = MinProbeFontSize; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
	{
		const float CandidateSingleLineHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, static_cast<float>(CandidateFontSize));
		const FVector2D CandidateWrappedSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			static_cast<float>(CandidateFontSize),
			Node->Text.Content,
			WidthLimit,
			bAutoWrapText);
		if (CandidateSingleLineHeight <= 0.0f
			|| CandidateWrappedSize.X <= 0.0f
			|| CandidateWrappedSize.Y <= 0.0f
			|| CandidateWrappedSize.X > WidthLimit + 0.5f)
		{
			continue;
		}

		const int32 CandidateLineCount = WrapHeight::EstimateWrappedLineCount(CandidateSingleLineHeight, CandidateWrappedSize);
		const float CandidateWidthSlack = FMath::Max(0.0f, WidthLimit - CandidateWrappedSize.X);
		const float CandidateHeightError = FMath::Abs(CandidateSingleLineHeight - TargetLineBoxHeight);
		const float CandidateDistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
		const float CandidateHeightSlack = bHasFixedHeight
			? AvailableHeight - CandidateWrappedSize.Y
			: TNumericLimits<float>::Max();
		const int32 SourceLineCount = bHasFixedHeight
			? FMath::Max(1, FMath::RoundToInt(AvailableHeight / FMath::Max(1.0f, TargetLineBoxHeight)))
			: 0;
		const int32 CandidateLineCountError = bHasFixedHeight
			? FMath::Abs(CandidateLineCount - SourceLineCount)
			: MAX_int32;
		const bool bSameLineCountAsBest = CandidateLineCount == BestLineCount;
		const bool bPreferHeightErrorBeforeWidthSlack = !bHasFixedHeight;
		const bool bBetterPrimaryMetric = bPreferHeightErrorBeforeWidthSlack
			? CandidateHeightError + 0.5f < BestHeightError
			: CandidateWidthSlack + 0.5f < BestWidthSlack;
		const bool bSamePrimaryMetric = bPreferHeightErrorBeforeWidthSlack
			? FMath::IsNearlyEqual(CandidateHeightError, BestHeightError, 0.5f)
			: FMath::IsNearlyEqual(CandidateWidthSlack, BestWidthSlack, 0.5f);
		const bool bBetterSecondaryMetric = bPreferHeightErrorBeforeWidthSlack
			? CandidateWidthSlack + 0.5f < BestWidthSlack
			: CandidateHeightError + 0.5f < BestHeightError;
		const bool bSameSecondaryMetric = bPreferHeightErrorBeforeWidthSlack
			? FMath::IsNearlyEqual(CandidateWidthSlack, BestWidthSlack, 0.5f)
			: FMath::IsNearlyEqual(CandidateHeightError, BestHeightError, 0.5f);

		if (BestFontSize == INDEX_NONE
			|| CandidateLineCount < BestLineCount
			|| (bSameLineCountAsBest && bBetterPrimaryMetric)
			|| (bSameLineCountAsBest
				&& bSamePrimaryMetric
				&& bBetterSecondaryMetric)
			|| (bSameLineCountAsBest
				&& bSamePrimaryMetric
				&& bSameSecondaryMetric
				&& CandidateDistanceToFallback < BestDistanceToFallback))
		{
			BestFontSize = CandidateFontSize;
			BestLineCount = CandidateLineCount;
			BestWidthSlack = CandidateWidthSlack;
			BestHeightError = CandidateHeightError;
			BestDistanceToFallback = CandidateDistanceToFallback;
		}

		if (!bHasFixedHeight || CandidateHeightSlack + KINDA_SMALL_NUMBER >= VerticalWrapSafetySlack)
		{
			const bool bCandidateIsWrappingShape = bAutoWrapText || CandidateLineCount > 1;
			const int32 BestSafeLineCountError = bHasFixedHeight && BestSafeLineCount != MAX_int32
				? FMath::Abs(BestSafeLineCount - SourceLineCount)
				: MAX_int32;
			const bool bBetterFixedHeightWrappingShape =
				bHasFixedHeight
				&& bCandidateIsWrappingShape
				&& (CandidateLineCountError < BestSafeLineCountError
					|| (CandidateLineCountError == BestSafeLineCountError
						&& CandidateLineCount < BestSafeLineCount));
			const bool bBetterSingleLineOrFlexibleHeight =
				(!bHasFixedHeight || !bCandidateIsWrappingShape)
				&& (CandidateLineCount < BestSafeLineCount
					|| (CandidateLineCount == BestSafeLineCount
						&& CandidateHeightSlack + 0.5f < BestSafeHeightSlack)
					|| (CandidateLineCount == BestSafeLineCount
						&& FMath::IsNearlyEqual(CandidateHeightSlack, BestSafeHeightSlack, 0.5f)
						&& CandidateWidthSlack + 0.5f < BestSafeWidthSlack)
					|| (CandidateLineCount == BestSafeLineCount
						&& FMath::IsNearlyEqual(CandidateHeightSlack, BestSafeHeightSlack, 0.5f)
						&& FMath::IsNearlyEqual(CandidateWidthSlack, BestSafeWidthSlack, 0.5f)
						&& CandidateHeightError + 0.5f < BestSafeHeightError)
					|| (CandidateLineCount == BestSafeLineCount
						&& FMath::IsNearlyEqual(CandidateHeightSlack, BestSafeHeightSlack, 0.5f)
						&& FMath::IsNearlyEqual(CandidateWidthSlack, BestSafeWidthSlack, 0.5f)
						&& FMath::IsNearlyEqual(CandidateHeightError, BestSafeHeightError, 0.5f)
						&& CandidateDistanceToFallback < BestSafeDistanceToFallback));
			if (BestSafeFontSize == INDEX_NONE
				|| bBetterFixedHeightWrappingShape
				|| bBetterSingleLineOrFlexibleHeight)
			{
				BestSafeFontSize = CandidateFontSize;
				BestSafeLineCount = CandidateLineCount;
				BestSafeHeightSlack = CandidateHeightSlack;
				BestSafeWidthSlack = CandidateWidthSlack;
				BestSafeHeightError = CandidateHeightError;
				BestSafeDistanceToFallback = CandidateDistanceToFallback;
			}
		}
	}

	return BestSafeFontSize != INDEX_NONE ? BestSafeFontSize : BestFontSize;
}

float ResolveTextFontSizeForUMGInternal(const TSharedPtr<FDesignNode>& Node, FString* OutResolutionPath)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}
	if (FallbackCalibration::ShouldUseDeclaredFontSizeDirectly(Node))
	{
		CacheTrace::LogResolvedTextDecision(Node, TEXT("declared-direct"), Node->Text.FontSize, Node->Text.FontSize, OutResolutionPath);
		return Node->Text.FontSize;
	}
	const float FallbackFontSize = FallbackCalibration::ResolveTextFontSizeFallback(Node);
	if (Node->Text.FontSize <= 0.0f || !TextMeasurementRuntimeProbe::IsAvailable())
	{
		CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-no-slate"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
		return FallbackFontSize;
	}

	const float TargetLineBoxHeight = LineBox::EstimateSingleLineTextHeight(Node);
	if (TargetLineBoxHeight <= 0.0f)
	{
		CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-no-linebox"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
		return FallbackFontSize;
	}

	const bool bApplyExplicitLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node, FallbackFontSize);
	if (SingleLineHeuristics::ShouldPreferIntrinsicSingleLineFallbackFontSize(Node))
	{
		CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-intrinsic-single-line"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
		return FallbackFontSize;
	}

	if (!bApplyExplicitLineHeight)
	{
		SingleLineWidthFit::FProbePlan ProbePlan;
		if (!SingleLineWidthFit::TryBuildProbePlan(Node, FallbackFontSize, ProbePlan))
		{
			CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-no-width-probe"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
			return FallbackFontSize;
		}

		auto MeasureWidth = [&](float CandidateFontSize) -> float
		{
			const float MeasuredWidth = RuntimeProbe::MeasureUMGTextBlockDesiredSize(Node, CandidateFontSize, Node->Text.Content, 0.0f, false).X;
			if (MeasuredWidth > 0.0f)
			{
				return MeasuredWidth;
			}

			const float WidthScale = Node->Transform.bSourceHasWidth
				&& TextMeasurement::IsWrappingTextMode(Node)
				&& FFontResolver::HasInvalidProjectFontAsset(Node->Text.FontFamily)
				? RuntimeProbe::UnresolvedFontFallbackMeasureWidthScale
				: RuntimeProbe::FallbackMeasureWidthScale;
			return TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).Len()
				* CandidateFontSize
				* WidthScale;
		};

		const int32 BestWrappedFitFontSize =
			SingleLineWidthFit::ResolveWrappedHeightConstrainedFontSize(Node, ProbePlan, MeasureWidth);
		if (BestWrappedFitFontSize != INDEX_NONE)
		{
			CacheTrace::LogResolvedTextDecision(Node, TEXT("wrapped-width-fit"), static_cast<float>(BestWrappedFitFontSize), FallbackFontSize, OutResolutionPath);
			return static_cast<float>(BestWrappedFitFontSize);
		}
		const int32 BestFontSize = ResolveBestWidthFitFontSize(
			FallbackFontSize,
			ProbePlan.MaxProbeFontSize,
			ProbePlan.WidthFitTarget,
			ProbePlan.bAllowClosestOvershoot,
			MeasureWidth);
		if (BestFontSize == INDEX_NONE)
		{
			CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-width-probe-miss"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
			return FallbackFontSize;
		}
		if (UE_LOG_ACTIVE(LogDesign2UMG, Verbose)
			&& !Node->Transform.bSourceHasWidth
			&& !Node->Text.Content.Contains(TEXT("\n"))
			&& !TextMeasurement::ShouldAutoWrapText(Node))
		{
			const int32 FallbackFloor = FMath::Max(1, FMath::FloorToInt(FallbackFontSize));
			const int32 FallbackCeil = FMath::Max(1, FMath::CeilToInt(FallbackFontSize));
			const int32 ProbePlusOne = FMath::Min(ProbePlan.MaxProbeFontSize, FallbackCeil + 1);
			UE_LOG(
				LogDesign2UMG,
				Verbose,
				TEXT("WidthProbe: node='%s' text='%s' target=%.1f raw=%.1f fallback=%.2f chosen=%d w_floor=%.2f w_ceil=%.2f w_plus1=%.2f"),
				*Node->Name,
				*CacheTrace::SummarizeTextForLog(Node->Text.Content),
				ProbePlan.WidthFitTarget,
				ProbePlan.TargetTextWidth,
				FallbackFontSize,
				BestFontSize,
				MeasureWidth(static_cast<float>(FallbackFloor)),
				MeasureWidth(static_cast<float>(FallbackCeil)),
				MeasureWidth(static_cast<float>(ProbePlusOne)));
		}
		if (SingleLineHeuristics::ShouldClampSingleLineWidthFitToFallback(Node, FallbackFontSize, BestFontSize))
		{
			const float ClampedFontSize = static_cast<float>(FMath::CeilToInt(FMath::Max(1.0f, FallbackFontSize - 0.05f)));
			CacheTrace::LogResolvedTextDecision(Node, TEXT("width-fit-clamped-to-fallback"), ClampedFontSize, FallbackFontSize, OutResolutionPath);
			return ClampedFontSize;
		}
		if (const int32 CappedFontSize = ResolveSourceWidthSingleLineGrowthCap(Node, FallbackFontSize, BestFontSize);
			CappedFontSize != INDEX_NONE)
		{
			CacheTrace::LogResolvedTextDecision(Node, TEXT("width-fit-capped-to-fallback"), static_cast<float>(CappedFontSize), FallbackFontSize, OutResolutionPath);
			return static_cast<float>(CappedFontSize);
		}
		if (const int32 HeightCappedFontSize = ResolveFixedHeightSingleLineWidthFitCap(Node, BestFontSize);
			HeightCappedFontSize != INDEX_NONE && HeightCappedFontSize < BestFontSize)
		{
			CacheTrace::LogResolvedTextDecision(Node, TEXT("width-fit-height-capped"), static_cast<float>(HeightCappedFontSize), FallbackFontSize, OutResolutionPath);
			return static_cast<float>(HeightCappedFontSize);
		}
		CacheTrace::LogResolvedTextDecision(Node, TEXT("width-fit"), static_cast<float>(BestFontSize), FallbackFontSize, OutResolutionPath);
		return static_cast<float>(BestFontSize);
	}

	const FString CalibrationKey = CacheTrace::BuildTextFontCalibrationKey(Node);
	const FString SharedStyleKey = SharedCalibration::ShouldParticipateInSharedTextStyleCalibration(Node)
		? CacheTrace::BuildTextFontSharedStyleKey(Node)
		: FString();
	const bool bConsumesSharedStyleScale = SharedCalibration::ShouldConsumeSharedTextStyleCalibration(Node);
	float SharedStyleScale = 0.0f;
	const bool bHasSharedStyleScale =
		!SharedStyleKey.IsEmpty() && CacheTrace::TryGetResolvedSharedTextFontScaleMin(SharedStyleKey, SharedStyleScale);
	float CachedScale = 0.0f;
	if (CacheTrace::TryGetResolvedTextFontScale(CalibrationKey, CachedScale))
	{
		float ResolvedScale = CachedScale;
		if (bConsumesSharedStyleScale && bHasSharedStyleScale)
		{
			ResolvedScale = FMath::Min(ResolvedScale, SharedStyleScale);
		}
		const float ResolvedFontSize = FMath::Max(1.0f, Node->Text.FontSize * ResolvedScale);
		CacheTrace::LogResolvedTextDecision(Node, TEXT("cached-scale"), ResolvedFontSize, FallbackFontSize, OutResolutionPath);
		return ResolvedFontSize;
	}

	if (bConsumesSharedStyleScale && bHasSharedStyleScale)
	{
		const float ResolvedFontSize = FMath::Max(1.0f, Node->Text.FontSize * SharedStyleScale);
		CacheTrace::LogResolvedTextDecision(Node, TEXT("shared-style-scale"), ResolvedFontSize, FallbackFontSize, OutResolutionPath);
		return ResolvedFontSize;
	}

	if (const int32 PreferredWrappedFontSize = ResolveVerticalAlignedWrappingFontSize(Node, FallbackFontSize, TargetLineBoxHeight);
		PreferredWrappedFontSize != INDEX_NONE)
	{
		const float ResolvedScale = static_cast<float>(PreferredWrappedFontSize) / Node->Text.FontSize;
		CacheTrace::StoreResolvedTextFontScale(CalibrationKey, ResolvedScale);
		if (!SharedStyleKey.IsEmpty())
		{
			CacheTrace::StoreResolvedSharedTextFontScaleMin(SharedStyleKey, ResolvedScale);
		}
		CacheTrace::LogResolvedTextDecision(Node, TEXT("vertical-wrap-fit"), static_cast<float>(PreferredWrappedFontSize), FallbackFontSize, OutResolutionPath);
		return static_cast<float>(PreferredWrappedFontSize);
	}

	auto MeasureHeight = [&](float CandidateFontSize) -> float
	{
		return RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, CandidateFontSize);
	};

	const int32 MaxProbeFontSize = RuntimeProbe::ResolveMaxProbeFontSize(Node->Text.FontSize, FallbackFontSize);
	const int32 BestFontSize = ResolveBestProbeFontSize(
		FallbackFontSize,
		MaxProbeFontSize,
		TargetLineBoxHeight,
		MeasureHeight);
	if (BestFontSize == INDEX_NONE)
	{
		CacheTrace::LogResolvedTextDecision(Node, TEXT("fallback-height-probe-miss"), FallbackFontSize, FallbackFontSize, OutResolutionPath);
		return FallbackFontSize;
	}

	if (TextContentRules::HasExplicitParagraphBreaks(Node) && TextMeasurement::IsWrappingTextMode(Node))
	{
		const float ResolvedFontSize = static_cast<float>(FMath::Max(1, BestFontSize - 1));
		CacheTrace::LogResolvedTextDecision(Node, TEXT("paragraph-wrap-adjust"), ResolvedFontSize, FallbackFontSize, OutResolutionPath);
		return ResolvedFontSize;
	}

	const float ResolvedScale = static_cast<float>(BestFontSize) / Node->Text.FontSize;
	CacheTrace::StoreResolvedTextFontScale(CalibrationKey, ResolvedScale);
	if (!SharedStyleKey.IsEmpty())
	{
		CacheTrace::StoreResolvedSharedTextFontScaleMin(SharedStyleKey, ResolvedScale);
	}
	CacheTrace::LogResolvedTextDecision(Node, TEXT("height-fit"), static_cast<float>(BestFontSize), FallbackFontSize, OutResolutionPath);
	return static_cast<float>(BestFontSize);
}
}
