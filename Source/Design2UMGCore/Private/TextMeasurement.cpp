// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"

#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "TextStyleUtils.h"

#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	TMap<FString, float> GResolvedTextFontScaleCache;

	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		if (Node->Width.IsFill())
		{
			return Node->Width.Value > 0.0f ? Node->Width.Value : ParentContentWidth;
		}

		return Node->Width.Value > 0.0f ? Node->Width.Value : 0.0f;
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

	int32 EstimateWrappedLineCount(float SingleLineHeight, const FVector2D& WrappedSize)
	{
		if (SingleLineHeight <= 0.0f || WrappedSize.Y <= 0.0f)
		{
			return 0;
		}

		return FMath::Max(1, FMath::RoundToInt(WrappedSize.Y / FMath::Max(1.0f, SingleLineHeight)));
	}

	float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->FontSize <= 0.0f)
		{
			return 0.0f;
		}

		const float BaseFontSize = Node->FontSize;
		const float LineHeightMultiplier = Node->LineHeight > 0.0f ? Node->LineHeight : 1.285714f;
		return FMath::Max(BaseFontSize, BaseFontSize * LineHeightMultiplier);
	}

	ETextJustify::Type ToSlateJustification(EDesignTextAlign TextAlign)
	{
		switch (TextAlign)
		{
		case EDesignTextAlign::Center: return ETextJustify::Center;
		case EDesignTextAlign::Right: return ETextJustify::Right;
		default: return ETextJustify::Left;
		}
	}

	FString NormalizeTextMeasurementFontFamilyKey(const FString& FontFamily)
	{
		FString Normalized = FontFamily.ToLower();
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		return Normalized;
	}

	bool ShouldUseDeclaredFontSizeDirectly(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->FontSize <= 0.0f)
		{
			return false;
		}

		const FString NormalizedFamily = NormalizeTextMeasurementFontFamilyKey(Node->FontFamily);
		if (NormalizedFamily != TEXT("roboto"))
		{
			return false;
		}

		const FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
		return AssetPath.StartsWith(TEXT("/Engine/EngineFonts/Roboto"), ESearchCase::IgnoreCase);
	}

	FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		FString Typeface = FFontResolver::FindBestTypeface(Node->FontFamily, Node->FontWeight);
		if (Typeface.IsEmpty() && !Node->FontWeight.IsEmpty())
		{
			Typeface = FDesignTypeMapper::MapFontWeight(Node->FontWeight).ToString();
		}
		return Typeface;
	}

	bool IsFigmaTruncateTextNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString* Value = Node->Metadata.Find(TEXT("figma.text.truncate"));
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	float ResolveTextFontSizeFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		const FString NormalizedFamily = NormalizeTextMeasurementFontFamilyKey(Node->FontFamily);
		if (NormalizedFamily == TEXT("funneldisplay"))
		{
			return Node->FontSize * 0.75f;
		}
		if (NormalizedFamily == TEXT("rubik"))
		{
			return Node->FontSize * 0.70f;
		}
		if (NormalizedFamily == TEXT("geist"))
		{
			return Node->FontSize * 0.71f;
		}

		return Node->FontSize * 0.75f;
	}

	FString BuildTextFontCalibrationKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		const FString LineHeightKey = Node->LineHeight > 0.0f
			? FString::Printf(TEXT("%.4f"), Node->LineHeight)
			: TEXT("default");
		const FString NormalizedFamily = NormalizeTextMeasurementFontFamilyKey(Node->FontFamily);
		const FString TypefaceName = ResolveTypefaceName(Node);
		return NormalizedFamily + TEXT("|") + TypefaceName + TEXT("|") + LineHeightKey + FString::Printf(TEXT("|%.1f"), Node->FontSize);
	}

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

		if (!Node->FontFamily.IsEmpty())
		{
			const FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
			if (!AssetPath.IsEmpty())
			{
				if (UObject* FontObject = LoadObject<UObject>(nullptr, *AssetPath))
				{
					FontInfo.FontObject = FontObject;
				}
			}
		}

		const FString Typeface = ResolveTypefaceName(Node);
		if (!Typeface.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*Typeface);
		}

		if (Node->LetterSpacing != 0.0f && FontInfo.Size > 0)
		{
			FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, FontInfo.Size);
		}

		return FontInfo;
	}

	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing = 0)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			if (Renderer)
			{
				TSharedRef<FSlateFontMeasure> Measure = Renderer->GetFontMeasureService();
				TSharedPtr<FDesignNode> MeasureNode = MakeShared<FDesignNode>();
				MeasureNode->FontFamily = FontFamily;
				MeasureNode->FontWeight = FontWeight;
				MeasureNode->FontSize = FontSize;
				FSlateFontInfo FontInfo = BuildSlateFontInfoForSize(MeasureNode, FontSize);
				FontInfo.LetterSpacing = LetterSpacing;
				const FVector2D Size = Measure->Measure(Text, FontInfo);
				if (Size.X > 0 && Size.Y > 0)
				{
					return Size;
				}
			}
		}

		return FVector2D(Text.Len() * FontSize * 0.6f, FontSize * 1.5f);
	}

	FVector2D MeasureUMGTextBlockDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		float FontSize,
		const FString& Text,
		float WrapTextAt = 0.0f,
		bool bAutoWrapText = false)
	{
		if (!Node.IsValid() || FontSize <= 0.0f || !FSlateApplication::IsInitialized())
		{
			return FVector2D::ZeroVector;
		}

		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		if (!Renderer)
		{
			return FVector2D::ZeroVector;
		}

		UTextBlock* ProbeTextBlock = NewObject<UTextBlock>();
		if (!ProbeTextBlock)
		{
			return FVector2D::ZeroVector;
		}

		ProbeTextBlock->SetText(FText::FromString(Text));
		ProbeTextBlock->SetFont(BuildSlateFontInfoForSize(Node, FontSize));
		ProbeTextBlock->SetJustification(ToSlateJustification(Node->TextAlign));
		const bool bApplyExplicitLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node, FontSize, WrapTextAt);
		ProbeTextBlock->SetLineHeightPercentage(bApplyExplicitLineHeight ? Node->LineHeight : 1.0f);
		ProbeTextBlock->SetApplyLineHeightToBottomLine(bApplyExplicitLineHeight);
		ProbeTextBlock->SetAutoWrapText(bAutoWrapText);
		ProbeTextBlock->SetWrapTextAt(WrapTextAt);

		TSharedRef<SWidget> SlateWidget = ProbeTextBlock->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);
		SlateWidget->SlatePrepass(1.0f);
		return SlateWidget->GetDesiredSize();
	}

	float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize)
	{
		return MeasureUMGTextBlockDesiredSize(Node, FontSize, TEXT("Hg")).Y;
	}

	bool ShouldUseSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node, float UnwrappedWidth, float ParentContentWidth)
	{
		if (!Node.IsValid() || Node->Content.Contains(TEXT("\n")))
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
		const float HeuristicFontSize = AppliedFontSize > 0.0f ? AppliedFontSize : Node->FontSize;
		const float HeuristicWidth = Node->Content.Len() * HeuristicFontSize * 0.55f;
		const float EffectiveWidth = UnwrappedWidth > 0.0f ? UnwrappedWidth : HeuristicWidth;
		return EffectiveWidth <= WidthLimit + 0.5f;
	}

	int32 ResolveVerticalAlignedWrappingFontSize(
		const TSharedPtr<FDesignNode>& Node,
		float FallbackFontSize,
		float TargetLineBoxHeight)
	{
		if (!Node.IsValid()
			|| Node->Content.Contains(TEXT("\n"))
			|| Node->TextAlignVertical == EDesignTextAlignVertical::Top)
		{
			return INDEX_NONE;
		}

		const float WidthLimit = ResolveTextWidthHint(Node, 0.0f);
		if (WidthLimit <= 0.0f)
		{
			return INDEX_NONE;
		}

		const int32 MaxProbeFontSize = FMath::Max(1, FMath::CeilToInt(FallbackFontSize));
		const int32 MinProbeFontSize = FMath::Min(
			MaxProbeFontSize,
			FMath::Max(1, FMath::FloorToInt(FallbackFontSize * 0.70f)));
		const bool bAutoWrapText = TextMeasurement::ShouldAutoWrapText(Node);

		int32 BestFontSize = INDEX_NONE;
		int32 BestLineCount = MAX_int32;
		float BestWidthSlack = TNumericLimits<float>::Max();
		float BestHeightError = TNumericLimits<float>::Max();
		float BestDistanceToFallback = TNumericLimits<float>::Max();

		for (int32 CandidateFontSize = MinProbeFontSize; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
		{
			const float CandidateSingleLineHeight = MeasureSingleLineTextBlockHeight(Node, static_cast<float>(CandidateFontSize));
			const FVector2D CandidateWrappedSize = MeasureUMGTextBlockDesiredSize(
				Node,
				static_cast<float>(CandidateFontSize),
				Node->Content,
				WidthLimit,
				bAutoWrapText);
			if (CandidateSingleLineHeight <= 0.0f
				|| CandidateWrappedSize.X <= 0.0f
				|| CandidateWrappedSize.Y <= 0.0f
				|| CandidateWrappedSize.X > WidthLimit + 0.5f)
			{
				continue;
			}

			const int32 CandidateLineCount = EstimateWrappedLineCount(CandidateSingleLineHeight, CandidateWrappedSize);
			const float CandidateWidthSlack = FMath::Max(0.0f, WidthLimit - CandidateWrappedSize.X);
			const float CandidateHeightError = FMath::Abs(CandidateSingleLineHeight - TargetLineBoxHeight);
			const float CandidateDistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);

			if (BestFontSize == INDEX_NONE
				|| CandidateLineCount < BestLineCount
				|| (CandidateLineCount == BestLineCount && CandidateWidthSlack + 0.5f < BestWidthSlack)
				|| (CandidateLineCount == BestLineCount
					&& FMath::IsNearlyEqual(CandidateWidthSlack, BestWidthSlack, 0.5f)
					&& CandidateHeightError + 0.5f < BestHeightError)
				|| (CandidateLineCount == BestLineCount
					&& FMath::IsNearlyEqual(CandidateWidthSlack, BestWidthSlack, 0.5f)
					&& FMath::IsNearlyEqual(CandidateHeightError, BestHeightError, 0.5f)
					&& CandidateDistanceToFallback < BestDistanceToFallback))
			{
				BestFontSize = CandidateFontSize;
				BestLineCount = CandidateLineCount;
				BestWidthSlack = CandidateWidthSlack;
				BestHeightError = CandidateHeightError;
				BestDistanceToFallback = CandidateDistanceToFallback;
			}
		}

		return BestFontSize;
	}
}

void TextMeasurement::ResetCache()
{
	GResolvedTextFontScaleCache.Reset();
}

bool TextMeasurement::IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node)
{
	if (IsFigmaTruncateTextNode(Node))
	{
		return false;
	}

	return Node.IsValid()
		&& (Node->TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->TextAlign == EDesignTextAlign::Justify);
}

bool TextMeasurement::ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth)
{
	if (!Node.IsValid() || Node->LineHeight <= 0.0f)
	{
		return false;
	}

	if (Node->Content.Contains(TEXT("\n")))
	{
		return true;
	}

	if (Node->FontSize >= 24.0f && !IsWrappingTextMode(Node))
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

	const float EffectiveProbeFontSize = ProbeFontSize > 0.0f ? ProbeFontSize : Node->FontSize;
	const int32 LetterSpacingTracking = (EffectiveProbeFontSize > 0.0f && Node->LetterSpacing != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, EffectiveProbeFontSize)
		: 0;
	const FVector2D UnwrappedSize = MeasureTextSize(
		Node->Content,
		EffectiveProbeFontSize,
		Node->FontFamily,
		Node->FontWeight,
		LetterSpacingTracking);
	return UnwrappedSize.X > WidthLimit + 0.5f;
}

float TextMeasurement::ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}
	if (ShouldUseDeclaredFontSizeDirectly(Node))
	{
		return Node->FontSize;
	}

	const float FallbackFontSize = ResolveTextFontSizeFallback(Node);
	if (Node->FontSize <= 0.0f || !FSlateApplication::IsInitialized())
	{
		return FallbackFontSize;
	}

	const float TargetLineBoxHeight = EstimateSingleLineTextHeight(Node);
	if (TargetLineBoxHeight <= 0.0f)
	{
		return FallbackFontSize;
	}

	if (!ShouldApplyExplicitLineHeight(Node, FallbackFontSize))
	{
		const float TargetTextWidth = (!Node->Width.IsFill() && Node->Width.Value > 0.0f && !Node->Content.Contains(TEXT("\n")))
			? Node->Width.Value
			: 0.0f;
		if (TargetTextWidth <= 0.0f || !FSlateApplication::IsInitialized())
		{
			return FallbackFontSize;
		}

		auto MeasureWidth = [&](float CandidateFontSize) -> float
		{
			return MeasureUMGTextBlockDesiredSize(Node, CandidateFontSize, Node->Content, 0.0f, false).X;
		};

		const int32 MaxProbeFontSize = FMath::Max(
			1,
			FMath::CeilToInt(FMath::Max(Node->FontSize * 1.5f, FallbackFontSize * 2.0f)));
		const int32 BestFontSize = ResolveBestProbeFontSize(
			FallbackFontSize,
			MaxProbeFontSize,
			TargetTextWidth,
			MeasureWidth);
		if (BestFontSize == INDEX_NONE)
		{
			return FallbackFontSize;
		}

		return static_cast<float>(BestFontSize);
	}

	const FString CalibrationKey = BuildTextFontCalibrationKey(Node);
	if (const float* CachedScale = GResolvedTextFontScaleCache.Find(CalibrationKey))
	{
		return FMath::Max(1.0f, Node->FontSize * (*CachedScale));
	}

	if (const int32 PreferredWrappedFontSize = ResolveVerticalAlignedWrappingFontSize(Node, FallbackFontSize, TargetLineBoxHeight);
		PreferredWrappedFontSize != INDEX_NONE)
	{
		const float ResolvedScale = static_cast<float>(PreferredWrappedFontSize) / Node->FontSize;
		GResolvedTextFontScaleCache.Add(CalibrationKey, ResolvedScale);
		return static_cast<float>(PreferredWrappedFontSize);
	}

	auto MeasureHeight = [&](float CandidateFontSize) -> float
	{
		return MeasureSingleLineTextBlockHeight(Node, CandidateFontSize);
	};

	const int32 MaxProbeFontSize = FMath::Max(
		1,
		FMath::CeilToInt(FMath::Max(Node->FontSize * 1.5f, FallbackFontSize * 2.0f)));
	const int32 BestFontSize = ResolveBestProbeFontSize(
		FallbackFontSize,
		MaxProbeFontSize,
		TargetLineBoxHeight,
		MeasureHeight);
	if (BestFontSize == INDEX_NONE)
	{
		return FallbackFontSize;
	}

	const float ResolvedScale = static_cast<float>(BestFontSize) / Node->FontSize;
	GResolvedTextFontScaleCache.Add(CalibrationKey, ResolvedScale);
	return static_cast<float>(BestFontSize);
}

float TextMeasurement::ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}

	if (IsFigmaTruncateTextNode(Node))
	{
		return 0.0f;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, ParentContentWidth);
	if (WidthLimit <= 0.0f)
	{
		return 0.0f;
	}

	const bool bNeedsExplicitWrap =
		Node->TextGrowth == EDesignTextGrowth::FixedWidth
		|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
		|| (Node->TextGrowth == EDesignTextGrowth::Auto
			&& Node->Width.IsFill()
			&& Node->Width.Value > 0.0f)
		|| Node->TextAlign == EDesignTextAlign::Justify
		|| Node->Content.Contains(TEXT("\n"));
	return bNeedsExplicitWrap ? WidthLimit : 0.0f;
}

bool TextMeasurement::ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node)
{
	return IsWrappingTextMode(Node);
}

FSlateFontInfo TextMeasurement::BuildSlateFontInfo(const TSharedPtr<FDesignNode>& Node)
{
	return BuildSlateFontInfoForSize(Node, ResolveTextFontSizeForUMG(Node));
}

FVector2D TextMeasurement::MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return FVector2D::ZeroVector;
	}

	const float AppliedFontSize = ResolveTextFontSizeForUMG(Node);
	const int32 LetterSpacingTracking = (AppliedFontSize > 0.0f && Node->LetterSpacing != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, AppliedFontSize)
		: 0;
	FVector2D FallbackSize = MeasureTextSize(
		Node->Content,
		AppliedFontSize > 0.0f ? AppliedFontSize : Node->FontSize,
		Node->FontFamily,
		Node->FontWeight,
		LetterSpacingTracking);

	if (ShouldUseSingleLineTextHeight(Node, FallbackSize.X, ParentContentWidth))
	{
		FallbackSize.Y = EstimateSingleLineTextHeight(Node);
		return FallbackSize;
	}

	const float WrapTextAt = ResolveTextWrapAt(Node, ParentContentWidth);
	if (WrapTextAt <= 0.0f && !Node->Content.Contains(TEXT("\n")))
	{
		return FallbackSize;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		if (Renderer)
		{
			const FSlateFontInfo FontInfo = BuildSlateFontInfo(Node);
			const FVector2D Size = MeasureUMGTextBlockDesiredSize(
				Node,
				FontInfo.Size,
				Node->Content,
				WrapTextAt,
				ShouldAutoWrapText(Node));
			if (Size.X > 0.0f && Size.Y > 0.0f)
			{
				return Size;
			}
		}
	}

	if (WrapTextAt > 0.0f && FallbackSize.X > WrapTextAt + 0.5f)
	{
		const float LineHeight = EstimateSingleLineTextHeight(Node);
		const int32 EstimatedLineCount = FMath::Max(1, FMath::CeilToInt(FallbackSize.X / FMath::Max(1.0f, WrapTextAt)));
		FallbackSize.X = WrapTextAt;
		FallbackSize.Y = LineHeight * EstimatedLineCount;
	}

	return FallbackSize;
}
