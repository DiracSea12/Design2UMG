// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"
#include "DesignSchemaSourceExtensions.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "SharedCoreSemantics.h"
#include "TextStyleUtils.h"
#include "TextMeasurementRuntimeProbe.h"

#include "Engine/Font.h"
#include "Misc/ScopeLock.h"
#include "Styling/SlateTypes.h"

namespace
{
	namespace SharedThresholds
	{
		constexpr float LargeNonWrappingFontThreshold = 24.0f;
		constexpr int32 CompactSingleLineLabelWordLimit = 2;
		constexpr float CompactSingleLineLabelWidthThreshold = 80.0f;
		constexpr float CompactSingleLineLabelWidthInset = 5.0f;
		constexpr float DefaultFigmaWidthInset = 4.0f;
	}

	namespace LineBox
	{
		constexpr float DefaultLineHeightMultiplier = 1.285714f;
	}

	namespace FallbackCalibration
	{
		constexpr float DefaultFontFallbackScale = 0.75f;
		constexpr float MinMeasuredFontFallbackScale = 0.5f;
		constexpr float MaxMeasuredFontFallbackScale = DefaultFontFallbackScale;
		constexpr float IntrinsicSingleLineSlackThreshold = 1.08f;
	}

	namespace RuntimeProbe
	{
		constexpr float FallbackMeasureWidthScale = 0.60f;
		constexpr float FallbackMeasureHeightScale = 1.5f;
		constexpr float DeclaredFontProbeScale = 1.5f;
		constexpr float FallbackFontProbeScale = 2.0f;

		FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing = 0);
		float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize);
		FVector2D MeasureUMGTextBlockDesiredSize(
			const TSharedPtr<FDesignNode>& Node,
			float FontSize,
			const FString& Text,
			float WrapTextAt,
			bool bAutoWrapText);
		int32 ResolveMaxProbeFontSize(float DeclaredFontSize, float FallbackFontSize);
	}

	namespace SingleLineHeuristics
	{
		constexpr float WidthHeuristicScale = 0.55f;
		bool IsHeavyFontWeight(const FString& FontWeight);
		int32 CountSingleLineTextWords(const TSharedPtr<FDesignNode>& Node);
		bool ShouldAllowClosestOvershootSingleLineWidthFit(const TSharedPtr<FDesignNode>& Node);
	}

	namespace SourceMetadata
	{
		bool IsFigmaSourceNode(const TSharedPtr<FDesignNode>& Node);
	}

	namespace WrapHeight
	{
		constexpr float MinWrapProbeScale = 0.70f;
	}

	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
	}

	namespace RuntimeProbe
	{
	constexpr float WidthFitTolerance = 0.5f;
	constexpr float WidthFitOvershootTolerance = 2.0f;

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
	}

	namespace WrapHeight
	{
	int32 EstimateWrappedLineCount(float SingleLineHeight, const FVector2D& WrappedSize)
	{
		if (SingleLineHeight <= 0.0f || WrappedSize.Y <= 0.0f)
		{
			return 0;
		}

		return FMath::Max(1, FMath::RoundToInt(WrappedSize.Y / FMath::Max(1.0f, SingleLineHeight)));
	}
	}

	namespace SharedThresholdOwners
	{
	bool ShouldApplyLargeNonWrappingExplicitLineHeight(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Text.LineHeight > 0.0f
			&& !Node->Text.Content.Contains(TEXT("\n"))
			&& Node->Text.FontSize >= SharedThresholds::LargeNonWrappingFontThreshold
			&& !TextMeasurement::IsWrappingTextMode(Node);
	}
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

	bool IsCompactSingleLineLabel(const TSharedPtr<FDesignNode>& Node, const float TargetTextWidth)
	{
		return Node.IsValid()
			&& !SingleLineHeuristics::IsHeavyFontWeight(Node->Text.FontWeight)
			&& SingleLineHeuristics::CountSingleLineTextWords(Node) <= SharedThresholds::CompactSingleLineLabelWordLimit
			&& TargetTextWidth <= SharedThresholds::CompactSingleLineLabelWidthThreshold;
	}

	bool TryBuildProbePlan(
		const TSharedPtr<FDesignNode>& Node,
		const float FallbackFontSize,
		FProbePlan& OutPlan)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		OutPlan.TargetTextWidth = (!Node->Transform.Width.IsFill()
			&& Node->Transform.Width.Value > 0.0f
			&& !Node->Text.Content.Contains(TEXT("\n")))
			? Node->Transform.Width.Value
			: 0.0f;
		if (OutPlan.TargetTextWidth <= 0.0f || !TextMeasurementRuntimeProbe::IsAvailable())
		{
			return false;
		}

		const bool bShouldApplyWidthInset =
			Node->Transform.bSourceHasWidth
			&& !TextMeasurement::ShouldAutoWrapText(Node)
			&& Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top
			&& SourceMetadata::IsFigmaSourceNode(Node);
		const float WidthInset = IsCompactSingleLineLabel(Node, OutPlan.TargetTextWidth)
			? SharedThresholds::CompactSingleLineLabelWidthInset
			: SharedThresholds::DefaultFigmaWidthInset;
		OutPlan.WidthFitTarget = bShouldApplyWidthInset
			? FMath::Max(0.0f, OutPlan.TargetTextWidth - WidthInset)
			: OutPlan.TargetTextWidth;
		OutPlan.MaxProbeFontSize = RuntimeProbe::ResolveMaxProbeFontSize(Node->Text.FontSize, FallbackFontSize);
		OutPlan.bConstrainWrappedHeight =
			TextMeasurement::ShouldAutoWrapText(Node)
			&& Node->Transform.Height.Value > 0.0f;
		OutPlan.bAllowClosestOvershoot =
			!Node->Transform.bSourceHasWidth
			&& SingleLineHeuristics::ShouldAllowClosestOvershootSingleLineWidthFit(Node);
		return true;
	}

	int32 ResolveWrappedHeightConstrainedFontSize(
		const TSharedPtr<FDesignNode>& Node,
		const FProbePlan& Plan,
		TFunctionRef<float(float)> MeasureWidth)
	{
		if (!Plan.bConstrainWrappedHeight)
		{
			return INDEX_NONE;
		}

		int32 BestWrappedFitFontSize = INDEX_NONE;
		float BestWrappedFitError = TNumericLimits<float>::Max();
		constexpr float WrappedHeightTolerance = 1.0f;
		for (int32 CandidateFontSize = 1; CandidateFontSize <= Plan.MaxProbeFontSize; ++CandidateFontSize)
		{
			const float CandidateWidth = MeasureWidth(static_cast<float>(CandidateFontSize));
			if (CandidateWidth <= 0.0f)
			{
				continue;
			}

			const FVector2D CandidateWrappedSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
				Node,
				static_cast<float>(CandidateFontSize),
				Node->Text.Content,
				Plan.TargetTextWidth,
				true);
			if (CandidateWrappedSize.X <= 0.0f || CandidateWrappedSize.Y <= 0.0f)
			{
				continue;
			}

			if (CandidateWrappedSize.Y > Node->Transform.Height.Value + WrappedHeightTolerance)
			{
				continue;
			}

			const float Error = FMath::Abs(CandidateWidth - Plan.TargetTextWidth);
			if (BestWrappedFitFontSize == INDEX_NONE
				|| Error + KINDA_SMALL_NUMBER < BestWrappedFitError
				|| (FMath::IsNearlyEqual(Error, BestWrappedFitError, 0.01f) && CandidateFontSize > BestWrappedFitFontSize))
			{
				BestWrappedFitFontSize = CandidateFontSize;
				BestWrappedFitError = Error;
			}
		}

		return BestWrappedFitFontSize;
	}
	}

	namespace FallbackCalibration
	{
	bool TryGetDefaultSlateTextFontAssetPath(FString& OutAssetPath)
	{
		const UObject* DefaultFontObject = FTextBlockStyle::GetDefault().Font.FontObject;
		if (!DefaultFontObject)
		{
			return false;
		}

		OutAssetPath = DefaultFontObject->GetPathName();
		return !OutAssetPath.IsEmpty();
	}

	bool IsDefaultSlateTextFontAssetPath(const FString& AssetPath)
	{
		FString DefaultFontAssetPath;
		return !AssetPath.IsEmpty()
			&& TryGetDefaultSlateTextFontAssetPath(DefaultFontAssetPath)
			&& AssetPath.Equals(DefaultFontAssetPath, ESearchCase::IgnoreCase);
	}
	}

	namespace RuntimeProbe
	{
	int32 ResolveMaxProbeFontSize(float DeclaredFontSize, float FallbackFontSize)
	{
		return FMath::Max(
			1,
			FMath::CeilToInt(FMath::Max(DeclaredFontSize * DeclaredFontProbeScale, FallbackFontSize * FallbackFontProbeScale)));
	}
	}

	namespace CacheTrace
	{
	TMap<FString, float> GResolvedTextFontScaleCache;
	TMap<FString, float> GResolvedSharedTextFontScaleMinCache;
	FCriticalSection GResolvedTextFontScaleCacheMutex;

	bool TryGetResolvedTextFontScale(const FString& CalibrationKey, float& OutScale)
	{
		FScopeLock Lock(&GResolvedTextFontScaleCacheMutex);
		if (const float* CachedScale = GResolvedTextFontScaleCache.Find(CalibrationKey))
		{
			OutScale = *CachedScale;
			return true;
		}
		return false;
	}

	void StoreResolvedTextFontScale(const FString& CalibrationKey, float Scale)
	{
		FScopeLock Lock(&GResolvedTextFontScaleCacheMutex);
		GResolvedTextFontScaleCache.Add(CalibrationKey, Scale);
	}

	bool TryGetResolvedSharedTextFontScaleMin(const FString& SharedKey, float& OutScale)
	{
		FScopeLock Lock(&GResolvedTextFontScaleCacheMutex);
		if (const float* CachedScale = GResolvedSharedTextFontScaleMinCache.Find(SharedKey))
		{
			OutScale = *CachedScale;
			return true;
		}
		return false;
	}

	void StoreResolvedSharedTextFontScaleMin(const FString& SharedKey, float Scale)
	{
		FScopeLock Lock(&GResolvedTextFontScaleCacheMutex);
		float& CachedScale = GResolvedSharedTextFontScaleMinCache.FindOrAdd(SharedKey, Scale);
		CachedScale = FMath::Min(CachedScale, Scale);
	}
	}

	namespace LineBox
	{
	float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->Text.FontSize <= 0.0f)
		{
			return 0.0f;
		}

		const float BaseFontSize = Node->Text.FontSize;
		const float LineHeightMultiplier = Node->Text.LineHeight > 0.0f ? Node->Text.LineHeight : DefaultLineHeightMultiplier;
		return FMath::Max(BaseFontSize, BaseFontSize * LineHeightMultiplier);
	}
	}

	namespace FallbackCalibration
	{
	FString NormalizeTextMeasurementFontFamilyKey(const FString& FontFamily)
	{
		FString Normalized = FontFamily.ToLower();
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		return Normalized;
	}

	float FindFamilySpecificTextFallbackScale(const FString& NormalizedFamily)
	{
		static const TPair<const TCHAR*, float> FamilyFallbackScales[] = {
			{ TEXT("rubik"), 0.70f },
			{ TEXT("geist"), 0.71f },
			{ TEXT("robotomono"), 0.73f },
			{ TEXT("funneldisplay"), 0.75f },
		};

		for (const TPair<const TCHAR*, float>& Entry : FamilyFallbackScales)
		{
			if (NormalizedFamily == Entry.Key)
			{
				return Entry.Value;
			}
		}

		return DefaultFontFallbackScale;
	}

	bool ShouldUseDeclaredFontSizeDirectly(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->Text.FontSize <= 0.0f)
		{
			return false;
		}

		const FString AssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
		return IsDefaultSlateTextFontAssetPath(AssetPath);
	}
	}

	namespace FontCalibration
	{
	FString ResolveTypefaceName(const FString& FontFamily, const FString& FontWeight)
	{
		FString Typeface = FFontResolver::FindBestTypeface(FontFamily, FontWeight);
		if (Typeface.IsEmpty() && !FontWeight.IsEmpty())
		{
			Typeface = FDesignTypeMapper::MapFontWeight(FontWeight).ToString();
		}
		return Typeface;
	}

	FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		return ResolveTypefaceName(Node->Text.FontFamily, Node->Text.FontWeight);
	}
	}

	namespace RuntimeProbe
	{
	TextMeasurementRuntimeProbe::FTextMeasureRequest BuildTextMeasureRequest(
		const FString& Text,
		const float FontSize,
		const FString& FontFamily,
		const FString& FontWeight,
		const int32 LetterSpacing)
	{
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = FFontResolver::FindFontAssetPath(FontFamily);
		Request.TypefaceName = FontCalibration::ResolveTypefaceName(FontFamily, FontWeight);
		Request.LetterSpacing = LetterSpacing;
		return Request;
	}

	TextMeasurementRuntimeProbe::FTextBlockMeasureRequest BuildTextBlockMeasureRequest(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize,
		const FString& Text,
		const float WrapTextAt,
		const bool bAutoWrapText)
	{
		TextMeasurementRuntimeProbe::FTextBlockMeasureRequest Request;
		if (!Node.IsValid())
		{
			return Request;
		}

		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
		Request.TypefaceName = FontCalibration::ResolveTypefaceName(Node);
		Request.LetterSpacing = (Node->Text.LetterSpacing != 0.0f && FontSize > 0.0f)
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontSize)
			: 0;
		Request.TextAlign = Node->Text.TextAlign;
		Request.bApplyExplicitLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node, FontSize, WrapTextAt);
		Request.LineHeightPercentage = Request.bApplyExplicitLineHeight ? Node->Text.LineHeight : 1.0f;
		Request.bAutoWrapText = bAutoWrapText;
		Request.WrapTextAt = WrapTextAt;
		return Request;
	}
	}

	namespace SingleLineHeuristics
	{
	bool IsHeavyFontWeight(const FString& FontWeight)
	{
		const FString Normalized = FontWeight.ToLower().TrimStartAndEnd();
		if (Normalized.IsEmpty())
		{
			return false;
		}

		if (Normalized.Contains(TEXT("bold")))
		{
			return true;
		}

		if (Normalized == TEXT("600")
			|| Normalized == TEXT("700")
			|| Normalized == TEXT("800")
			|| Normalized == TEXT("900"))
		{
			return true;
		}

		return false;
	}

	bool IsBoldDisplayFontWeight(const FString& FontWeight)
	{
		const FString Normalized = FontWeight.ToLower().TrimStartAndEnd();
		if (Normalized.Contains(TEXT("bold")))
		{
			return true;
		}

		return Normalized == TEXT("700")
			|| Normalized == TEXT("800")
			|| Normalized == TEXT("900");
	}
	}

	namespace CacheTrace
	{
	FString SummarizeTextForLog(const FString& Text)
	{
		FString Summary = TextMeasurement::PrepareRuntimeTextContent(Text);
		if (Summary.Len() > 48)
		{
			Summary = Summary.Left(48) + TEXT("...");
		}
		return Summary;
	}

	void LogResolvedTextDecision(
		const TSharedPtr<FDesignNode>& Node,
		const TCHAR* Reason,
		const float ResolvedFontSize,
		const float FallbackFontSize,
		FString* OutResolutionPath = nullptr)
	{
		if (!Node.IsValid())
		{
			return;
		}

		UE_LOG(
			LogDesign2UMG,
			Log,
			TEXT("TextDecision: node='%s' text='%s' reason=%s resolved=%.1f fallback=%.1f source=%.1f family='%s' weight='%s' sourceWidth=%d widthMode=%d width=%.1f heightMode=%d height=%.1f wrap=%d valign=%d expandedRef=%d"),
			*Node->Name,
			*SummarizeTextForLog(Node->Text.Content),
			Reason,
			ResolvedFontSize,
			FallbackFontSize,
			Node->Text.FontSize,
			*Node->Text.FontFamily,
			*Node->Text.FontWeight,
			Node->Transform.bSourceHasWidth ? 1 : 0,
			static_cast<int32>(Node->Transform.Width.Mode),
			Node->Transform.Width.Value,
			static_cast<int32>(Node->Transform.Height.Mode),
			Node->Transform.Height.Value,
			TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
			static_cast<int32>(Node->Text.TextAlignVertical),
			Node->Source().bExpandedRefInstance ? 1 : 0);
		if (OutResolutionPath)
		{
			*OutResolutionPath = Reason;
		}
	}
	}

	namespace SourceMetadata
	{
	bool IsFigmaTruncateTextNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString* Value = Node->Metadata.Find(TEXT("source.text.truncate"));
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	bool IsFigmaSourceNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (const FString* Provider = Node->Metadata.Find(TEXT("source.provider")))
		{
			return Provider->Equals(TEXT("figma"), ESearchCase::IgnoreCase);
		}

		return false;
	}
	}

	namespace TextContentRules
	{
	bool HasExplicitParagraphBreaks(const FString& Text)
	{
		return Text.Contains(TEXT("\n\n"))
			|| Text.Contains(TEXT("\r\n\r\n"))
			|| Text.Contains(TEXT("\r\r"));
	}

	bool HasExplicitParagraphBreaks(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && HasExplicitParagraphBreaks(Node->Text.Content);
	}
	}

	namespace SingleLineHeuristics
	{
	bool IsLongSentenceLikeSingleLineText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (RuntimeText.Len() >= 28)
		{
			return true;
		}

		TArray<FString> Words;
		RuntimeText.ParseIntoArrayWS(Words);
		return Words.Num() >= 4;
	}

	int32 CountSingleLineTextWords(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0;
		}

		TArray<FString> Words;
		TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd().ParseIntoArrayWS(Words);
		return Words.Num();
	}

	bool StartsWithAsciiDigit(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (FChar::IsWhitespace(Char))
			{
				continue;
			}

			return FChar::IsDigit(Char);
		}

		return false;
	}

	bool ContainsAsciiDigit(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (FChar::IsDigit(Char))
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsLowercaseLetter(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (FChar::IsLower(Char))
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsLetter(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (FChar::IsAlpha(Char))
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsMetricMetaSymbol(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			switch (Char)
			{
			case TEXT('+'):
			case TEXT('-'):
			case TEXT('%'):
			case TEXT('$'):
			case TEXT('#'):
			case TEXT(':'):
			case TEXT('/'):
			case TEXT('['):
			case TEXT(']'):
			case TEXT('_'):
			case 0x00B7:
				return true;
			default:
				break;
			}
		}

		return false;
	}

	bool EndsWithQuestionMark(const FString& Text)
	{
		FString Trimmed = Text;
		Trimmed.TrimEndInline();
		return Trimmed.EndsWith(TEXT("?"));
	}

	bool IsPrimaryValueLikeSingleLineText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (RuntimeText.IsEmpty()
			|| !ContainsAsciiDigit(RuntimeText)
			|| !IsBoldDisplayFontWeight(Node->Text.FontWeight))
		{
			return false;
		}

		const int32 WordCount = CountSingleLineTextWords(Node);
		return Node->Text.FontSize >= 18.0f
			&& WordCount > 0
			&& WordCount <= 2
			&& RuntimeText.Len() <= 12;
	}

	bool IsCompactNumericLikeSingleLineText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (RuntimeText.IsEmpty() || !ContainsAsciiDigit(RuntimeText))
		{
			return false;
		}

		if (IsPrimaryValueLikeSingleLineText(Node))
		{
			return false;
		}

		const int32 WordCount = CountSingleLineTextWords(Node);
		if (WordCount <= 0)
		{
			return true;
		}

		if (ContainsMetricMetaSymbol(RuntimeText))
		{
			return true;
		}

		if (WordCount == 1)
		{
			return true;
		}

		if (WordCount == 2 && !ContainsLowercaseLetter(RuntimeText))
		{
			return true;
		}

		return RuntimeText.Len() <= 12 && WordCount <= 2;
	}

	bool IsSentenceCaseNumericLabelText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (RuntimeText.IsEmpty()
			|| !ContainsLetter(RuntimeText)
			|| !ContainsAsciiDigit(RuntimeText)
			|| ContainsMetricMetaSymbol(RuntimeText)
			|| !Node->Text.FontWeight.IsEmpty())
		{
			return false;
		}

		const int32 WordCount = CountSingleLineTextWords(Node);
		return WordCount >= 3
			&& WordCount <= 4
			&& !StartsWithAsciiDigit(RuntimeText)
			&& ContainsLowercaseLetter(RuntimeText);
	}

	bool IsExpandedRefControlLabelText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Source().bExpandedRefInstance)
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (RuntimeText.IsEmpty()
			|| !ContainsLetter(RuntimeText)
			|| ContainsAsciiDigit(RuntimeText))
		{
			return false;
		}

		const int32 WordCount = CountSingleLineTextWords(Node);
		if (WordCount <= 0 || WordCount > 3)
		{
			return false;
		}

		return Node->Transform.Height.Value >= 18.0f
			&& Node->Transform.Height.Value <= 24.0f;
	}

	bool IsRoomyQuestionPromptText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || IsHeavyFontWeight(Node->Text.FontWeight))
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		if (!EndsWithQuestionMark(RuntimeText))
		{
			return false;
		}

		const int32 WordCount = CountSingleLineTextWords(Node);
		return WordCount >= 4
			&& WordCount <= 5
			&& Node->Text.FontSize >= 15.0f
			&& Node->Transform.Height.Value >= 22.0f;
	}

	bool ShouldAllowClosestOvershootSingleLineWidthFit(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Text.Content.Contains(TEXT("\n"))
			|| TextMeasurement::IsWrappingTextMode(Node)
			|| Node->Transform.bSourceHasWidth)
		{
			return false;
		}

		if (IsExpandedRefControlLabelText(Node))
		{
			return true;
		}

		if (Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top)
		{
			return false;
		}

		if (IsPrimaryValueLikeSingleLineText(Node))
		{
			return true;
		}

		if (IsCompactNumericLikeSingleLineText(Node))
		{
			return false;
		}

		return IsSentenceCaseNumericLabelText(Node)
			|| IsRoomyQuestionPromptText(Node);
	}

	bool IsCompactBoldTokenText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
		return IsBoldDisplayFontWeight(Node->Text.FontWeight)
			&& Node->Text.FontSize >= 20.0f
			&& Node->Text.FontSize <= 24.0f
			&& !RuntimeText.IsEmpty()
			&& !RuntimeText.Contains(TEXT(" "))
			&& RuntimeText.Len() <= 3;
	}

	bool IsExpandedRefIntrinsicWidthClampCandidate(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Source().bExpandedRefInstance)
		{
			return false;
		}

		const FString NormalizedWeight = Node->Text.FontWeight.ToLower().TrimStartAndEnd();
		if (!NormalizedWeight.IsEmpty() && NormalizedWeight != TEXT("normal"))
		{
			return false;
		}

		const float SourceHeight = Node->Transform.Height.Value;
		return SourceHeight >= 17.5f
			&& SourceHeight <= 18.5f;
	}

	bool ShouldClampSingleLineWidthFitToFallback(const TSharedPtr<FDesignNode>& Node, float FallbackFontSize, int32 BestFontSize)
	{
		if (!Node.IsValid()
			|| Node->Text.Content.Contains(TEXT("\n"))
			|| TextMeasurement::IsWrappingTextMode(Node)
			|| Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top
			|| Node->Transform.bSourceHasWidth
			|| BestFontSize >= FMath::CeilToInt(FallbackFontSize))
		{
			return false;
		}

		if (IsExpandedRefIntrinsicWidthClampCandidate(Node))
		{
			return true;
		}

		return IsCompactBoldTokenText(Node);
	}

	bool ShouldUseMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const bool bBehavesLikeSingleLine =
			!Node->Text.Content.Contains(TEXT("\n"))
			&& !TextMeasurement::IsWrappingTextMode(Node);
		if (!bBehavesLikeSingleLine)
		{
			return true;
		}

		if (Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top)
		{
			return false;
		}

		return IsLongSentenceLikeSingleLineText(Node);
	}

	bool ShouldPreferIntrinsicSingleLineFallbackFontSize(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Text.Content.Contains(TEXT("\n"))
			|| TextMeasurement::IsWrappingTextMode(Node)
			|| Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top
			|| Node->Transform.bSourceHasWidth
			|| !Node->Source().bExpandedRefInstance
			|| Node->Text.FontSize < 12.0f
			|| IsHeavyFontWeight(Node->Text.FontWeight))
		{
			return false;
		}

		const float WidthHint = ResolveTextWidthHint(Node, 0.0f);
		if (WidthHint <= 0.0f)
		{
			return true;
		}

		const float FamilyFallbackFontSize =
			Node->Text.FontSize * FallbackCalibration::FindFamilySpecificTextFallbackScale(
				FallbackCalibration::NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily));
		if (FamilyFallbackFontSize <= 0.0f)
		{
			return false;
		}

		const int32 LetterSpacingTracking = Node->Text.LetterSpacing != 0.0f
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(
				Node->Text.LetterSpacing,
				Node->Text.FontSize,
				FamilyFallbackFontSize)
			: 0;
		const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
			Node->Text.Content,
			FamilyFallbackFontSize,
			Node->Text.FontFamily,
			Node->Text.FontWeight,
			LetterSpacingTracking);
		if (UnwrappedSize.X <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		return WidthHint >= UnwrappedSize.X * FallbackCalibration::IntrinsicSingleLineSlackThreshold;
	}
	}

	namespace FallbackCalibration
	{
	TOptional<float> TryResolveMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Text.FontSize <= 0.0f
			|| !TextMeasurementRuntimeProbe::IsAvailable())
		{
			return {};
		}

		float BestScale = 1.0f;
		bool bHasScale = false;

		const float TargetLineBoxHeight = LineBox::EstimateSingleLineTextHeight(Node);
		if (TargetLineBoxHeight > 0.0f)
		{
			const float MeasuredLineHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, Node->Text.FontSize);
			if (MeasuredLineHeight > KINDA_SMALL_NUMBER)
			{
				BestScale = TargetLineBoxHeight / MeasuredLineHeight;
				bHasScale = true;
			}
		}

		const float WidthHint = ResolveTextWidthHint(Node, 0.0f);
		if (!Node->Text.Content.IsEmpty() && WidthHint > 0.0f)
		{
			const int32 LetterSpacingTracking = Node->Text.LetterSpacing != 0.0f
				? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, Node->Text.FontSize)
				: 0;
			const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
				Node->Text.Content,
				Node->Text.FontSize,
				Node->Text.FontFamily,
				Node->Text.FontWeight,
				LetterSpacingTracking);
			if (UnwrappedSize.X > KINDA_SMALL_NUMBER)
			{
				const bool bHasWidthConstraint =
					TextMeasurement::IsWrappingTextMode(Node)
					|| Node->Text.Content.Contains(TEXT("\n"))
					|| WidthHint > 0.0f;
				if (bHasWidthConstraint)
				{
					const float WidthScale = WidthHint / UnwrappedSize.X;
					BestScale = bHasScale ? FMath::Min(BestScale, WidthScale) : WidthScale;
					bHasScale = true;
				}
			}
		}

		if (!bHasScale)
		{
			return {};
		}

		return FMath::Clamp(BestScale, MinMeasuredFontFallbackScale, MaxMeasuredFontFallbackScale);
	}

	float ResolveTextFontSizeFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		float FallbackScale = FindFamilySpecificTextFallbackScale(
			NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily));
		if (SingleLineHeuristics::ShouldUseMeasuredTextFontFallbackScale(Node)
			&& !SingleLineHeuristics::ShouldPreferIntrinsicSingleLineFallbackFontSize(Node))
		{
			if (const TOptional<float> MeasuredScale = TryResolveMeasuredTextFontFallbackScale(Node))
			{
				FallbackScale = FMath::Min(FallbackScale, MeasuredScale.GetValue());
			}
		}

		return Node->Text.FontSize * FallbackScale;
	}
	}

	namespace CacheTrace
	{
	FString BuildTextFontCalibrationKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		const FString LineHeightKey = Node->Text.LineHeight > 0.0f
			? FString::Printf(TEXT("%.4f"), Node->Text.LineHeight)
			: TEXT("default");
		const FString NormalizedFamily = FallbackCalibration::NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily);
		const FString TypefaceName = FontCalibration::ResolveTypefaceName(Node);
		const FString LetterSpacingKey = FString::Printf(TEXT("%.4f"), Node->Text.LetterSpacing);
		const FString WidthKey = FString::Printf(
			TEXT("%d:%.2f"),
			static_cast<int32>(Node->Transform.Width.Mode),
			Node->Transform.Width.Value);
		const FString HeightKey = FString::Printf(
			TEXT("%d:%.2f"),
			static_cast<int32>(Node->Transform.Height.Mode),
			Node->Transform.Height.Value);
		return FString::Printf(
			TEXT("%s|%s|%s|%.1f|ls=%s|w=%s|h=%s|grow=%d|align=%d|valign=%d|wrap=%d|truncate=%d"),
			*NormalizedFamily,
			*TypefaceName,
			*LineHeightKey,
			Node->Text.FontSize,
			*LetterSpacingKey,
			*WidthKey,
			*HeightKey,
			static_cast<int32>(Node->Text.TextGrowth),
			static_cast<int32>(Node->Text.TextAlign),
			static_cast<int32>(Node->Text.TextAlignVertical),
			TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
			SourceMetadata::IsFigmaTruncateTextNode(Node) ? 1 : 0);
	}

	FString BuildTextFontSharedStyleKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		const FString LineHeightKey = Node->Text.LineHeight > 0.0f
			? FString::Printf(TEXT("%.4f"), Node->Text.LineHeight)
			: TEXT("default");
		const FString NormalizedFamily = FallbackCalibration::NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily);
		const FString TypefaceName = FontCalibration::ResolveTypefaceName(Node);
		const FString LetterSpacingKey = FString::Printf(TEXT("%.4f"), Node->Text.LetterSpacing);
		return FString::Printf(
			TEXT("%s|%s|%s|%.1f|ls=%s|grow=%d|align=%d|valign=%d|wrap=%d|truncate=%d"),
			*NormalizedFamily,
			*TypefaceName,
			*LineHeightKey,
			Node->Text.FontSize,
			*LetterSpacingKey,
			static_cast<int32>(Node->Text.TextGrowth),
			static_cast<int32>(Node->Text.TextAlign),
			static_cast<int32>(Node->Text.TextAlignVertical),
			TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
			SourceMetadata::IsFigmaTruncateTextNode(Node) ? 1 : 0);
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

	namespace RuntimeProbe
	{
	FSlateFontInfo BuildSlateFontInfoForSize(const TSharedPtr<FDesignNode>& Node, float FontSize)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (!Node.IsValid())
		{
			return FontInfo;
		}

		if (FontSize > 0.0f)
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
		}

		if (!Node->Text.FontFamily.IsEmpty())
		{
			const FString AssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
			if (!AssetPath.IsEmpty())
			{
				if (UObject* FontObject = LoadObject<UObject>(nullptr, *AssetPath))
				{
					FontInfo.FontObject = FontObject;
				}
			}
		}

		const FString Typeface = FontCalibration::ResolveTypefaceName(Node);
		if (!Typeface.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*Typeface);
		}

		if (Node->Text.LetterSpacing != 0.0f && FontInfo.Size > 0)
		{
			FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, FontInfo.Size);
		}

		return FontInfo;
	}

	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing)
	{
		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
		FVector2D MeasuredSize = FVector2D::ZeroVector;
		if (TextMeasurementRuntimeProbe::TryMeasureText(
			BuildTextMeasureRequest(RuntimeText, FontSize, FontFamily, FontWeight, LetterSpacing),
			MeasuredSize))
		{
			return MeasuredSize;
		}

		return FVector2D(RuntimeText.Len() * FontSize * FallbackMeasureWidthScale, FontSize * FallbackMeasureHeightScale);
	}

	FVector2D MeasureUMGTextBlockDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		float FontSize,
		const FString& Text,
		float WrapTextAt = 0.0f,
		bool bAutoWrapText = false)
	{
		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
		if (!Node.IsValid() || FontSize <= 0.0f)
		{
			return FVector2D::ZeroVector;
		}

		FVector2D MeasuredSize = FVector2D::ZeroVector;
		return TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(
			BuildTextBlockMeasureRequest(Node, FontSize, RuntimeText, WrapTextAt, bAutoWrapText),
			MeasuredSize)
			? MeasuredSize
			: FVector2D::ZeroVector;
	}

	float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize)
	{
		return MeasureUMGTextBlockDesiredSize(Node, FontSize, TEXT("Hg")).Y;
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
			const float CandidateSingleLineHeight = MeasureSingleLineTextBlockHeight(Node, static_cast<float>(CandidateFontSize));
			const FVector2D CandidateWrappedSize = MeasureUMGTextBlockDesiredSize(
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
				if (BestSafeFontSize == INDEX_NONE
					|| CandidateLineCount < BestSafeLineCount
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
						&& CandidateDistanceToFallback < BestSafeDistanceToFallback))
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
				return RuntimeProbe::MeasureUMGTextBlockDesiredSize(Node, CandidateFontSize, Node->Text.Content, 0.0f, false).X;
			};

			const int32 BestWrappedFitFontSize =
				SingleLineWidthFit::ResolveWrappedHeightConstrainedFontSize(Node, ProbePlan, MeasureWidth);
			if (BestWrappedFitFontSize != INDEX_NONE)
			{
				CacheTrace::LogResolvedTextDecision(Node, TEXT("wrapped-width-fit"), static_cast<float>(BestWrappedFitFontSize), FallbackFontSize, OutResolutionPath);
				return static_cast<float>(BestWrappedFitFontSize);
			}
			const int32 BestFontSize = !Node->Transform.bSourceHasWidth
				? RuntimeProbe::ResolveBestWidthFitFontSize(
					FallbackFontSize,
					ProbePlan.MaxProbeFontSize,
					ProbePlan.TargetTextWidth,
					ProbePlan.bAllowClosestOvershoot,
					MeasureWidth)
				: RuntimeProbe::ResolveBestWidthFitFontSize(
					FallbackFontSize,
					ProbePlan.MaxProbeFontSize,
					ProbePlan.WidthFitTarget,
					false,
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
					TEXT("WidthProbe: node='%s' text='%s' target=%.1f fallback=%.2f chosen=%d w_floor=%.2f w_ceil=%.2f w_plus1=%.2f"),
					*Node->Name,
					*CacheTrace::SummarizeTextForLog(Node->Text.Content),
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

		if (const int32 PreferredWrappedFontSize = RuntimeProbe::ResolveVerticalAlignedWrappingFontSize(Node, FallbackFontSize, TargetLineBoxHeight);
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
		const int32 BestFontSize = RuntimeProbe::ResolveBestProbeFontSize(
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
}

void TextMeasurement::ResetCache()
{
	FScopeLock Lock(&CacheTrace::GResolvedTextFontScaleCacheMutex);
	CacheTrace::GResolvedTextFontScaleCache.Reset();
	CacheTrace::GResolvedSharedTextFontScaleMinCache.Reset();
}

FString TextMeasurement::PrepareRuntimeTextContent(const FString& Text)
{
	FString Result = Text;
	Result.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Result.ReplaceInline(TEXT("\r"), TEXT("\n"));
	return Result;
}

bool TextMeasurement::IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node)
{
	if (SourceMetadata::IsFigmaTruncateTextNode(Node))
	{
		return false;
	}

	return Node.IsValid()
		&& (Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->Text.TextAlign == EDesignTextAlign::Justify);
}

bool TextMeasurement::ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth)
{
	if (!Node.IsValid() || Node->Text.LineHeight <= 0.0f)
	{
		return false;
	}

	if (Node->Text.Content.Contains(TEXT("\n")))
	{
		return true;
	}

	if (SharedThresholdOwners::ShouldApplyLargeNonWrappingExplicitLineHeight(Node))
	{
		return true;
	}

	if (!IsWrappingTextMode(Node))
	{
		return false;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, ParentContentWidth);
	if (WidthLimit <= 0.0f)
	{
		return false;
	}

	const float EffectiveProbeFontSize = ProbeFontSize > 0.0f ? ProbeFontSize : Node->Text.FontSize;
	const int32 LetterSpacingTracking = (EffectiveProbeFontSize > 0.0f && Node->Text.LetterSpacing != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, EffectiveProbeFontSize)
		: 0;
	const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
		Node->Text.Content,
		EffectiveProbeFontSize,
		Node->Text.FontFamily,
		Node->Text.FontWeight,
		LetterSpacingTracking);
	return UnwrappedSize.X > WidthLimit + 0.5f;
}

float TextMeasurement::ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node)
{
	return RuntimeProbe::ResolveTextFontSizeForUMGInternal(Node, nullptr);
}

float TextMeasurement::ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}

	if (SourceMetadata::IsFigmaTruncateTextNode(Node))
	{
		return 0.0f;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, ParentContentWidth);
	if (WidthLimit <= 0.0f)
	{
		return 0.0f;
	}

	const bool bNeedsExplicitWrap =
		Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth
		|| Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight
		|| (Node->Text.TextGrowth == EDesignTextGrowth::Auto
			&& Node->Transform.Width.IsFill()
			&& Node->Transform.Width.Value > 0.0f)
		|| Node->Text.TextAlign == EDesignTextAlign::Justify
		|| Node->Text.Content.Contains(TEXT("\n"));
	return bNeedsExplicitWrap ? WidthLimit : 0.0f;
}

bool TextMeasurement::ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node)
{
	return IsWrappingTextMode(Node);
}

FVector2D TextMeasurement::MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return FVector2D::ZeroVector;
	}

	const float AppliedFontSize = ResolveTextFontSizeForUMG(Node);
	const int32 LetterSpacingTracking = (AppliedFontSize > 0.0f && Node->Text.LetterSpacing != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, AppliedFontSize)
		: 0;
	FVector2D FallbackSize = RuntimeProbe::MeasureTextSize(
		PrepareRuntimeTextContent(Node->Text.Content),
		AppliedFontSize > 0.0f ? AppliedFontSize : Node->Text.FontSize,
		Node->Text.FontFamily,
		Node->Text.FontWeight,
		LetterSpacingTracking);

	if (RuntimeProbe::ShouldUseSingleLineTextHeight(Node, FallbackSize.X, ParentContentWidth))
	{
		FallbackSize.Y = LineBox::EstimateSingleLineTextHeight(Node);
		return FallbackSize;
	}

	const float WrapTextAt = ResolveTextWrapAt(Node, ParentContentWidth);
	if (WrapTextAt <= 0.0f && !Node->Text.Content.Contains(TEXT("\n")))
	{
		return FallbackSize;
	}

	const FVector2D WrappedSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
		Node,
		AppliedFontSize > 0.0f ? AppliedFontSize : Node->Text.FontSize,
		PrepareRuntimeTextContent(Node->Text.Content),
		WrapTextAt,
		ShouldAutoWrapText(Node));
	if (WrappedSize.X > 0.0f && WrappedSize.Y > 0.0f)
	{
		return WrappedSize;
	}

	if (WrapTextAt > 0.0f && FallbackSize.X > WrapTextAt + 0.5f)
	{
		const float LineHeight = LineBox::EstimateSingleLineTextHeight(Node);
		const int32 EstimatedLineCount = FMath::Max(1, FMath::CeilToInt(FallbackSize.X / FMath::Max(1.0f, WrapTextAt)));
		FallbackSize.X = WrapTextAt;
		FallbackSize.Y = LineHeight * EstimatedLineCount;
	}

	return FallbackSize;
}
