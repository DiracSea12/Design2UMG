// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "Design2UMGLog.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "TextMeasurementRuntimeProbe.h"

#include "Misc/ScopeLock.h"

namespace
{
	bool IsFigmaTruncateTextNodeForCacheKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString* Value = Node->Metadata.Find(TEXT("source.text.truncate"));
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	FString NormalizeFontFamilyKeyForCacheKey(const FString& FontFamily)
	{
		FString Normalized = FontFamily.ToLower();
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		return Normalized;
	}

	FString ResolveTypefaceNameForCacheKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		FString Typeface = FFontResolver::FindBestTypeface(Node->Text.FontFamily, Node->Text.FontWeight);
		if (Typeface.IsEmpty() && !Node->Text.FontWeight.IsEmpty())
		{
			Typeface = DesignTypeMapper::MapFontWeight(Node->Text.FontWeight).ToString();
		}
		return Typeface;
	}

	FString SerializeFiniteKeyFloat(const float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return FString();
		}

		const float Scaled = Value * 10000.0f;
		if (!FMath::IsFinite(Scaled))
		{
			return FString();
		}

		float Quantized = FMath::RoundToFloat(Scaled) / 10000.0f;
		if (FMath::IsNearlyZero(Quantized, 0.00005f))
		{
			Quantized = 0.0f;
		}
		return FString::Printf(TEXT("%.4f"), Quantized);
	}

	FString BuildMetricTokenForCacheKey(const FontCalibration::FLocalFontIdentityPayload& Payload, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Payload.bHasRequiredIdentity)
		{
			return FString();
		}

		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.Text = TEXT("Hg");
		Request.FontSize = Node->Text.FontSize;
		Request.FontAssetPath = FontCalibration::ResolveMetricFontAssetPath(Payload);
		Request.TypefaceName = Payload.ResolvedTypeface;
		Request.bRequiresResolvedFontAsset = true;
		const TextMeasurementRuntimeProbe::FSlateTextMetricFingerprint Metric =
			TextMeasurementRuntimeProbe::MeasureSlateTextMetricFingerprint(Request);
		return TextMeasurementRuntimeProbe::SerializeSlateTextMetricToken(Metric);
	}
}

namespace CacheTrace
{
	FString BuildBaselineFallbackScaleKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		FontCalibration::FLocalFontIdentityPayload Payload;
		const bool bHasLocalIdentity = FontCalibration::BuildLocalFontIdentityPayload(Node, Payload);
		const FString FontIdentity = bHasLocalIdentity
			? FontCalibration::BuildFontIdentityKeyToken(Payload)
			: NormalizeFontFamilyKeyForCacheKey(Node->Text.FontFamily);
		const FString TypefaceName = ResolveTypefaceNameForCacheKey(Node);
		const FString LineHeightKey = Node->Text.LineHeight > 0.0f
			? SerializeFiniteKeyFloat(Node->Text.LineHeight)
			: TEXT("default");
		if (LineHeightKey.IsEmpty())
		{
			return FString();
		}
		const FString LetterSpacingKey = Node->Text.HasLetterSpacingValue()
			? SerializeFiniteKeyFloat(Node->Text.LetterSpacing)
			: FString(TEXT("unset"));
		if (LetterSpacingKey.IsEmpty())
		{
			return FString();
		}
		const FString FontSizeKey = SerializeFiniteKeyFloat(Node->Text.FontSize);
		if (FontSizeKey.IsEmpty())
		{
			return FString();
		}
		const FString MetricToken = bHasLocalIdentity ? BuildMetricTokenForCacheKey(Payload, Node) : FString();
		const bool bApplyExplicitLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node, Node->Text.FontSize);
		return FString::Printf(
			TEXT("%s|%s|fs=%s|lh=%s|ls=%s|explicit=%d%s%s"),
			*FontIdentity,
			*TypefaceName,
			*FontSizeKey,
			*LineHeightKey,
			*LetterSpacingKey,
			bApplyExplicitLineHeight ? 1 : 0,
			MetricToken.IsEmpty() ? TEXT("") : TEXT("|"),
			*MetricToken);
	}

	bool FResolvedScaleCacheOwner::TryGetResolvedTextScale(const FString& CalibrationKey, float& OutScale) const
	{
		FScopeLock Lock(&Mutex);
		if (const float* CachedScale = ResolvedTextScaleByCalibrationKey.Find(CalibrationKey))
		{
			OutScale = *CachedScale;
			return true;
		}
		return false;
	}

	void FResolvedScaleCacheOwner::StoreResolvedTextScale(const FString& CalibrationKey, float Scale)
	{
		FScopeLock Lock(&Mutex);
		ResolvedTextScaleByCalibrationKey.Add(CalibrationKey, Scale);
	}

	bool FResolvedScaleCacheOwner::TryGetSharedStyleMinScale(const FString& SharedKey, float& OutScale) const
	{
		FScopeLock Lock(&Mutex);
		if (const float* CachedScale = SharedStyleMinScaleByStyleKey.Find(SharedKey))
		{
			OutScale = *CachedScale;
			return true;
		}
		return false;
	}

	void FResolvedScaleCacheOwner::StoreSharedStyleMinScale(const FString& SharedKey, float Scale)
	{
		FScopeLock Lock(&Mutex);
		float& CachedScale = SharedStyleMinScaleByStyleKey.FindOrAdd(SharedKey, Scale);
		CachedScale = FMath::Min(CachedScale, Scale);
	}

	bool FResolvedScaleCacheOwner::TryGetBaselineFallbackScale(const FString& ProbeKey, float& OutScale) const
	{
		FScopeLock Lock(&Mutex);
		if (const float* CachedScale = BaselineFallbackScaleByProbeKey.Find(ProbeKey))
		{
			OutScale = *CachedScale;
			return true;
		}
		return false;
	}

	void FResolvedScaleCacheOwner::StoreBaselineFallbackScale(const FString& ProbeKey, float Scale)
	{
		FScopeLock Lock(&Mutex);
		BaselineFallbackScaleByProbeKey.Add(ProbeKey, Scale);
	}

	void FResolvedScaleCacheOwner::Reset()
	{
		FScopeLock Lock(&Mutex);
		ResolvedTextScaleByCalibrationKey.Reset();
		SharedStyleMinScaleByStyleKey.Reset();
		BaselineFallbackScaleByProbeKey.Reset();
	}

#if WITH_AUTOMATION_WORKER
	TextMeasurement::Testing::FTextMeasurementCacheStats FResolvedScaleCacheOwner::BuildStats() const
	{
		FScopeLock Lock(&Mutex);
		TextMeasurement::Testing::FTextMeasurementCacheStats Stats;
		Stats.ResolvedScaleEntries = ResolvedTextScaleByCalibrationKey.Num();
		Stats.SharedStyleScaleEntries = SharedStyleMinScaleByStyleKey.Num();
		Stats.BaselineFallbackEntries = BaselineFallbackScaleByProbeKey.Num();
		return Stats;
	}
#endif

	FResolvedScaleCacheOwner GResolvedScaleCache;

	bool TryGetResolvedTextFontScale(const FString& CalibrationKey, float& OutScale)
	{
		return GResolvedScaleCache.TryGetResolvedTextScale(CalibrationKey, OutScale);
	}

	void StoreResolvedTextFontScale(const FString& CalibrationKey, float Scale)
	{
		GResolvedScaleCache.StoreResolvedTextScale(CalibrationKey, Scale);
	}

	bool TryGetResolvedSharedTextFontScaleMin(const FString& SharedKey, float& OutScale)
	{
		return GResolvedScaleCache.TryGetSharedStyleMinScale(SharedKey, OutScale);
	}

	void StoreResolvedSharedTextFontScaleMin(const FString& SharedKey, float Scale)
	{
		GResolvedScaleCache.StoreSharedStyleMinScale(SharedKey, Scale);
	}

	bool TryGetBaselineFallbackScale(const FString& ProbeKey, float& OutScale)
	{
		return GResolvedScaleCache.TryGetBaselineFallbackScale(ProbeKey, OutScale);
	}

	void StoreBaselineFallbackScale(const FString& ProbeKey, float Scale)
	{
		GResolvedScaleCache.StoreBaselineFallbackScale(ProbeKey, Scale);
	}

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
		FString* OutResolutionPath)
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

	FString BuildTextFontCalibrationKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		const FString LineHeightKey = Node->Text.LineHeight > 0.0f
			? FString::Printf(TEXT("%.4f"), Node->Text.LineHeight)
			: TEXT("default");
		const FString NormalizedFamily = NormalizeFontFamilyKeyForCacheKey(Node->Text.FontFamily);
		const FString TypefaceName = ResolveTypefaceNameForCacheKey(Node);
		const FString LetterSpacingKey = Node->Text.HasLetterSpacingValue()
			? FString::Printf(TEXT("%.4f"), Node->Text.LetterSpacing)
			: FString(TEXT("unset"));
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
			IsFigmaTruncateTextNodeForCacheKey(Node) ? 1 : 0);
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
		const FString NormalizedFamily = NormalizeFontFamilyKeyForCacheKey(Node->Text.FontFamily);
		const FString TypefaceName = ResolveTypefaceNameForCacheKey(Node);
		const FString LetterSpacingKey = Node->Text.HasLetterSpacingValue()
			? FString::Printf(TEXT("%.4f"), Node->Text.LetterSpacing)
			: FString(TEXT("unset"));
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
			IsFigmaTruncateTextNodeForCacheKey(Node) ? 1 : 0);
	}
}

void TextMeasurement::ResetCache()
{
	// Keep the raw cache reset as a low-level primitive for focused tests and boundary wrappers.
	CacheTrace::GResolvedScaleCache.Reset();
}

void TextMeasurement::ResetCacheForConversionBoundary()
{
	// Shared conversion code should prefer this named boundary instead of calling ResetCache() directly.
	ResetCache();
}

void TextMeasurement::ResetCacheForAutomationBoundary()
{
	// Shared automation code should prefer this named boundary instead of calling ResetCache() directly.
	ResetCache();
}

#if WITH_AUTOMATION_WORKER
TextMeasurement::Testing::FTextMeasurementCacheStats TextMeasurement::Testing::GetCacheStats()
{
	return CacheTrace::GResolvedScaleCache.BuildStats();
}

FString TextMeasurement::Testing::BuildBaselineFallbackScaleKey(const TSharedPtr<FDesignNode>& Node)
{
	return CacheTrace::BuildBaselineFallbackScaleKey(Node);
}
#endif
