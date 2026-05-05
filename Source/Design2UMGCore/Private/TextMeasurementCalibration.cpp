// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "SharedCoreSemantics.h"
#include "TextMeasurementRuntimeProbe.h"
#include "TextStyleUtils.h"

#include "Design2UMGLog.h"
#include "Styling/SlateTypes.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
	}
}

namespace LineBox
{
namespace
{
	// Historical fallback lock: a 14px default text run maps to an 18px single-line box,
	// so the shared multiplier stays at 18 / 14 = 1.285714.
	constexpr float DefaultLineHeightMultiplier = 1.285714f;
}

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
namespace
{
	// Historical unresolved-font ceiling: fallback text should not grow past 3 / 4 of the
	// declared size unless a tighter measured scale proves it.
	constexpr float DefaultFontFallbackScale = 0.75f;
	// Measured fallback should not collapse below half of the declared size.
	constexpr float MinMeasuredFontFallbackScale = 0.5f;
	constexpr float MaxMeasuredFontFallbackScale = DefaultFontFallbackScale;
}

extern const float IntrinsicSingleLineSlackThreshold = 1.08f;

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
	// Phase-10 local rollback: keep the last known no-regression family table
	// until a shared calibration rule proves parity across the full formal gate.
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

float FindFamilySpecificTextFallbackScale()
{
	return FindFamilySpecificTextFallbackScale(FString());
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

float ResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (const TOptional<float> SemanticScale = TryResolveSemanticSingleLineFallbackScale(Node))
	{
		return SemanticScale.GetValue();
	}

	float FallbackScale = DefaultFontFallbackScale;
	if (const TOptional<float> BaselineScale = TryResolveBaselineTextFontFallbackScale(Node))
	{
		FallbackScale = FMath::Min(FallbackScale, BaselineScale.GetValue());
	}

	return FallbackScale;
}

float ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (const TOptional<float> SemanticScale = TryResolveSemanticSingleLineFallbackScale(Node))
	{
		return SemanticScale.GetValue();
	}

	float FallbackScale = DefaultFontFallbackScale;
	if (const TOptional<float> BaselineScale = TryResolveBaselineTextFontFallbackScale(Node))
	{
		FallbackScale = FMath::Min(FallbackScale, BaselineScale.GetValue());
	}

	return FallbackScale;
}

TOptional<float> TryResolveSemanticSingleLineFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| Node->Transform.bSourceHasWidth
		|| Node->Transform.Width.IsFill()
		|| Node->Text.Content.Contains(TEXT("\n"))
		|| TextMeasurement::IsWrappingTextMode(Node))
	{
		return {};
	}

	if (SingleLineHeuristics::IsExpandedRefIntrinsicSingleLineText(Node))
	{
		// Expanded component text splits into two stable runtime shapes: proportional
		// content rows need the tighter line-box scale, while mono/status chips keep the
		// historical default ceiling. The split is glyph-metric based, not font-name based.
		return SingleLineHeuristics::ShouldUseTightExpandedRefIntrinsicScale(Node)
			? 0.71f
			: DefaultFontFallbackScale;
	}

	// Dense all-caps metric tokens behave like compact UI identifiers rather than prose.
	// Keep them below the broad 0.75 ceiling without routing on a specific font family.
	if (SingleLineHeuristics::IsDenseTechnicalTokenText(Node))
	{
		return 0.73f;
	}

	return {};
}

TOptional<float> TryResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| !TextMeasurementRuntimeProbe::IsAvailable())
	{
		return {};
	}

	const FString ProbeKey = CacheTrace::BuildBaselineFallbackScaleKey(Node);
	float CachedScale = 0.0f;
	if (!ProbeKey.IsEmpty() && CacheTrace::TryGetBaselineFallbackScale(ProbeKey, CachedScale))
	{
		return CachedScale;
	}

	const float TargetLineBoxHeight = LineBox::EstimateSingleLineTextHeight(Node);
	if (TargetLineBoxHeight <= KINDA_SMALL_NUMBER)
	{
		return {};
	}

	const float MeasuredLineHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, Node->Text.FontSize);
	if (MeasuredLineHeight <= KINDA_SMALL_NUMBER)
	{
		return {};
	}

	const float BaselineScale = FMath::Clamp(
		TargetLineBoxHeight / MeasuredLineHeight,
		MinMeasuredFontFallbackScale,
		MaxMeasuredFontFallbackScale);
	if (!ProbeKey.IsEmpty())
	{
		CacheTrace::StoreBaselineFallbackScale(ProbeKey, BaselineScale);
	}

	return BaselineScale;
}

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

namespace FontCalibration
{
FString ResolveTypefaceName(const FString& FontFamily, const FString& FontWeight)
{
	FString Typeface = FFontResolver::FindBestTypeface(FontFamily, FontWeight);
	if (Typeface.IsEmpty() && !FontWeight.IsEmpty())
	{
		Typeface = DesignTypeMapper::MapFontWeight(FontWeight).ToString();
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


#if WITH_AUTOMATION_WORKER
float TextMeasurement::Testing::EstimateSingleLineHeight(const TSharedPtr<FDesignNode>& Node)
{
	return LineBox::EstimateSingleLineTextHeight(Node);
}

float TextMeasurement::Testing::ResolveBaselineFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	return FallbackCalibration::ResolveBaselineTextFontFallbackScale(Node);
}

float TextMeasurement::Testing::ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	return FallbackCalibration::ResolveLineBoxFallbackScale(Node);
}

float TextMeasurement::Testing::ResolveSingleLineWidthFitTarget(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Transform.Width.IsFill()
		|| Node->Transform.Width.Value <= 0.0f
		|| Node->Text.Content.Contains(TEXT("\n")))
	{
		return 0.0f;
	}

	const bool bShouldApplySourceWidthInset =
		Node->Transform.bSourceHasWidth
		&& !TextMeasurement::ShouldAutoWrapText(Node)
		&& Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top
		&& SourceMetadata::IsFigmaSourceNode(Node);
	const float WidthInset = bShouldApplySourceWidthInset
		? (SingleLineWidthFit::IsCompactSingleLineLabel(Node, Node->Transform.Width.Value)
			? SharedThresholds::CompactSingleLineLabelWidthInset
			: SharedThresholds::DefaultFigmaWidthInset)
		: 0.0f;
	return bShouldApplySourceWidthInset
		? FMath::Max(0.0f, Node->Transform.Width.Value - WidthInset)
		: Node->Transform.Width.Value;
}

int32 TextMeasurement::Testing::ResolveBestWidthFitFontSizeFromWidths(
	float FallbackFontSize,
	int32 MaxProbeFontSize,
	float TargetWidth,
	bool bAllowClosestOvershoot,
	const TArray<float>& CandidateWidths)
{
	return RuntimeProbeSizing::ResolveBestWidthFitFontSize(
		FallbackFontSize,
		MaxProbeFontSize,
		TargetWidth,
		bAllowClosestOvershoot,
		[&CandidateWidths](float CandidateFontSize) -> float
		{
			const int32 CandidateIndex = FMath::RoundToInt(CandidateFontSize);
			return CandidateWidths.IsValidIndex(CandidateIndex) ? CandidateWidths[CandidateIndex] : 0.0f;
		});
}

FVector2D TextMeasurement::Testing::MeasureFallbackTextSize(const FString& Text, float FontSize)
{
	if (FontSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	return FVector2D(
		RuntimeText.Len() * FontSize * RuntimeProbe::FallbackMeasureWidthScale,
		FontSize * RuntimeProbe::FallbackMeasureHeightScale);
}

float TextMeasurement::Testing::EstimateSingleLineHeuristicWidth(const FString& Text, float FontSize)
{
	if (FontSize <= 0.0f)
	{
		return 0.0f;
	}

	return TextMeasurement::PrepareRuntimeTextContent(Text).Len() * FontSize * SingleLineHeuristics::WidthHeuristicScale;
}
#endif
