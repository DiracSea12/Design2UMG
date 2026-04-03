// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"

#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "TextStyleUtils.h"

#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeLock.h"

namespace
{
	TMap<FString, float> GResolvedTextFontScaleCache;
	TMap<FString, float> GResolvedSharedTextFontScaleMinCache;
	FCriticalSection GResolvedTextFontScaleCacheMutex;

	constexpr float DefaultLineHeightMultiplier = 1.285714f;
	constexpr float DefaultFontFallbackScale = 0.75f;
	constexpr float MinMeasuredFontFallbackScale = 0.5f;
	constexpr float MaxMeasuredFontFallbackScale = DefaultFontFallbackScale;
	constexpr float FallbackMeasureWidthScale = 0.60f;
	constexpr float SingleLineWidthHeuristicScale = 0.55f;
	constexpr float FallbackMeasureHeightScale = 1.5f;
	constexpr float MinWrapProbeScale = 0.70f;
	constexpr float DeclaredFontProbeScale = 1.5f;
	constexpr float FallbackFontProbeScale = 2.0f;
	constexpr float LargeNonWrappingFontThreshold = 24.0f;

	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing = 0);
	float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize);

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

	int32 ResolveMaxProbeFontSize(float DeclaredFontSize, float FallbackFontSize)
	{
		return FMath::Max(
			1,
			FMath::CeilToInt(FMath::Max(DeclaredFontSize * DeclaredFontProbeScale, FallbackFontSize * FallbackFontProbeScale)));
	}

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

	float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->FontSize <= 0.0f)
		{
			return 0.0f;
		}

		const float BaseFontSize = Node->FontSize;
		const float LineHeightMultiplier = Node->LineHeight > 0.0f ? Node->LineHeight : DefaultLineHeightMultiplier;
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
		if (!Node.IsValid() || Node->FontSize <= 0.0f)
		{
			return false;
		}

		const FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
		return IsDefaultSlateTextFontAssetPath(AssetPath);
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

	bool HasExplicitParagraphBreaks(const FString& Text)
	{
		return Text.Contains(TEXT("\n\n"))
			|| Text.Contains(TEXT("\r\n\r\n"))
			|| Text.Contains(TEXT("\r\r"));
	}

	bool HasExplicitParagraphBreaks(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && HasExplicitParagraphBreaks(Node->Content);
	}


	TOptional<float> TryResolveMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->FontSize <= 0.0f
			|| !FSlateApplication::IsInitialized())
		{
			return {};
		}

		float BestScale = 1.0f;
		bool bHasScale = false;

		const float TargetLineBoxHeight = EstimateSingleLineTextHeight(Node);
		if (TargetLineBoxHeight > 0.0f)
		{
			const float MeasuredLineHeight = MeasureSingleLineTextBlockHeight(Node, Node->FontSize);
			if (MeasuredLineHeight > KINDA_SMALL_NUMBER)
			{
				BestScale = TargetLineBoxHeight / MeasuredLineHeight;
				bHasScale = true;
			}
		}

		const float WidthHint = ResolveTextWidthHint(Node, 0.0f);
		if (!Node->Content.IsEmpty() && WidthHint > 0.0f)
		{
			const int32 LetterSpacingTracking = Node->LetterSpacing != 0.0f
				? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, Node->FontSize)
				: 0;
			const FVector2D UnwrappedSize = MeasureTextSize(
				Node->Content,
				Node->FontSize,
				Node->FontFamily,
				Node->FontWeight,
				LetterSpacingTracking);
			if (UnwrappedSize.X > KINDA_SMALL_NUMBER)
			{
				const bool bHasWidthConstraint =
					TextMeasurement::IsWrappingTextMode(Node)
					|| Node->Content.Contains(TEXT("\n"))
					|| (!Node->Width.IsFill() && Node->Width.Value > 0.0f);
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

		return Node->FontSize * FindFamilySpecificTextFallbackScale(
			NormalizeTextMeasurementFontFamilyKey(Node->FontFamily));
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
		const FString LetterSpacingKey = FString::Printf(TEXT("%.4f"), Node->LetterSpacing);
		const FString WidthKey = FString::Printf(
			TEXT("%d:%.2f"),
			static_cast<int32>(Node->Width.Mode),
			Node->Width.Value);
		const FString HeightKey = FString::Printf(
			TEXT("%d:%.2f"),
			static_cast<int32>(Node->Height.Mode),
			Node->Height.Value);
		return FString::Printf(
			TEXT("%s|%s|%s|%.1f|ls=%s|w=%s|h=%s|grow=%d|align=%d|valign=%d|wrap=%d|truncate=%d"),
			*NormalizedFamily,
			*TypefaceName,
			*LineHeightKey,
			Node->FontSize,
			*LetterSpacingKey,
			*WidthKey,
			*HeightKey,
			static_cast<int32>(Node->TextGrowth),
			static_cast<int32>(Node->TextAlign),
			static_cast<int32>(Node->TextAlignVertical),
			TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
			IsFigmaTruncateTextNode(Node) ? 1 : 0);
	}

	FString BuildTextFontSharedStyleKey(const TSharedPtr<FDesignNode>& Node)
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
		const FString LetterSpacingKey = FString::Printf(TEXT("%.4f"), Node->LetterSpacing);
		return FString::Printf(
			TEXT("%s|%s|%s|%.1f|ls=%s|grow=%d|align=%d|valign=%d|wrap=%d|truncate=%d"),
			*NormalizedFamily,
			*TypefaceName,
			*LineHeightKey,
			Node->FontSize,
			*LetterSpacingKey,
			static_cast<int32>(Node->TextGrowth),
			static_cast<int32>(Node->TextAlign),
			static_cast<int32>(Node->TextAlignVertical),
			TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
			IsFigmaTruncateTextNode(Node) ? 1 : 0);
	}

	bool ShouldParticipateInSharedTextStyleCalibration(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || IsFigmaTruncateTextNode(Node))
		{
			return false;
		}

		if (Node->TextAlignVertical == EDesignTextAlignVertical::Top)
		{
			return false;
		}

		return Node->TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->TextAlign == EDesignTextAlign::Justify;
	}

	bool ShouldConsumeSharedTextStyleCalibration(const TSharedPtr<FDesignNode>& Node)
	{
		return ShouldParticipateInSharedTextStyleCalibration(Node)
			&& Node->Content.Contains(TEXT("\n"));
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

	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing)
	{
		const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
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
				const FVector2D Size = Measure->Measure(RuntimeText, FontInfo);
				if (Size.X > 0 && Size.Y > 0)
				{
					return Size;
				}
			}
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

		ProbeTextBlock->SetText(FText::FromString(RuntimeText));
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
		const float HeuristicWidth = Node->Content.Len() * HeuristicFontSize * SingleLineWidthHeuristicScale;
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

		const int32 MaxProbeFontSize = FMath::Max(
			1,
			FMath::CeilToInt(FMath::Max(FallbackFontSize, Node->FontSize)));
		const int32 MinProbeFontSize = FMath::Min(
			MaxProbeFontSize,
			FMath::Max(1, FMath::FloorToInt(FallbackFontSize * MinWrapProbeScale)));
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
	FScopeLock Lock(&GResolvedTextFontScaleCacheMutex);
	GResolvedTextFontScaleCache.Reset();
	GResolvedSharedTextFontScaleMinCache.Reset();
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

	if (Node->FontSize >= LargeNonWrappingFontThreshold && !IsWrappingTextMode(Node))
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

	const bool bApplyExplicitLineHeight = ShouldApplyExplicitLineHeight(Node, FallbackFontSize);

	if (!bApplyExplicitLineHeight)
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

		const int32 MaxProbeFontSize = ResolveMaxProbeFontSize(Node->FontSize, FallbackFontSize);
		const bool bConstrainWrappedHeight =
			ShouldAutoWrapText(Node)
			&& Node->Height.Value > 0.0f;
		int32 BestWrappedFitFontSize = INDEX_NONE;
		float BestWrappedFitError = TNumericLimits<float>::Max();
		constexpr float WrappedHeightTolerance = 1.0f;

		if (bConstrainWrappedHeight)
		{
			for (int32 CandidateFontSize = 1; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
			{
				const float CandidateWidth = MeasureWidth(static_cast<float>(CandidateFontSize));
				if (CandidateWidth <= 0.0f)
				{
					continue;
				}

				const FVector2D CandidateWrappedSize = MeasureUMGTextBlockDesiredSize(
					Node,
					static_cast<float>(CandidateFontSize),
					Node->Content,
					TargetTextWidth,
					true);
				if (CandidateWrappedSize.X <= 0.0f || CandidateWrappedSize.Y <= 0.0f)
				{
					continue;
				}

				if (CandidateWrappedSize.Y > Node->Height.Value + WrappedHeightTolerance)
				{
					continue;
				}

				const float Error = FMath::Abs(CandidateWidth - TargetTextWidth);
				if (BestWrappedFitFontSize == INDEX_NONE
					|| Error + KINDA_SMALL_NUMBER < BestWrappedFitError
					|| (FMath::IsNearlyEqual(Error, BestWrappedFitError, 0.01f) && CandidateFontSize > BestWrappedFitFontSize))
				{
					BestWrappedFitFontSize = CandidateFontSize;
					BestWrappedFitError = Error;
				}
			}
		}

		if (BestWrappedFitFontSize != INDEX_NONE)
		{
			return static_cast<float>(BestWrappedFitFontSize);
		}
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
	const FString SharedStyleKey = ShouldParticipateInSharedTextStyleCalibration(Node)
		? BuildTextFontSharedStyleKey(Node)
		: FString();
	const bool bConsumesSharedStyleScale = ShouldConsumeSharedTextStyleCalibration(Node);
	float SharedStyleScale = 0.0f;
	const bool bHasSharedStyleScale =
		!SharedStyleKey.IsEmpty() && TryGetResolvedSharedTextFontScaleMin(SharedStyleKey, SharedStyleScale);
	float CachedScale = 0.0f;
	if (TryGetResolvedTextFontScale(CalibrationKey, CachedScale))
	{
		float ResolvedScale = CachedScale;
		if (bConsumesSharedStyleScale && bHasSharedStyleScale)
		{
			ResolvedScale = FMath::Min(ResolvedScale, SharedStyleScale);
		}
		return FMath::Max(1.0f, Node->FontSize * ResolvedScale);
	}

	if (bConsumesSharedStyleScale && bHasSharedStyleScale)
	{
		return FMath::Max(1.0f, Node->FontSize * SharedStyleScale);
	}

	if (const int32 PreferredWrappedFontSize = ResolveVerticalAlignedWrappingFontSize(Node, FallbackFontSize, TargetLineBoxHeight);
		PreferredWrappedFontSize != INDEX_NONE)
	{
		const float ResolvedScale = static_cast<float>(PreferredWrappedFontSize) / Node->FontSize;
		StoreResolvedTextFontScale(CalibrationKey, ResolvedScale);
		if (!SharedStyleKey.IsEmpty())
		{
			StoreResolvedSharedTextFontScaleMin(SharedStyleKey, ResolvedScale);
		}
		return static_cast<float>(PreferredWrappedFontSize);
	}

	auto MeasureHeight = [&](float CandidateFontSize) -> float
	{
		return MeasureSingleLineTextBlockHeight(Node, CandidateFontSize);
	};

	const int32 MaxProbeFontSize = ResolveMaxProbeFontSize(Node->FontSize, FallbackFontSize);
	const int32 BestFontSize = ResolveBestProbeFontSize(
		FallbackFontSize,
		MaxProbeFontSize,
		TargetLineBoxHeight,
		MeasureHeight);
	if (BestFontSize == INDEX_NONE)
	{
		return FallbackFontSize;
	}

	if (HasExplicitParagraphBreaks(Node) && IsWrappingTextMode(Node))
	{
		return static_cast<float>(FMath::Max(1, BestFontSize - 1));
	}

	const float ResolvedScale = static_cast<float>(BestFontSize) / Node->FontSize;
	StoreResolvedTextFontScale(CalibrationKey, ResolvedScale);
	if (!SharedStyleKey.IsEmpty())
	{
		StoreResolvedSharedTextFontScaleMin(SharedStyleKey, ResolvedScale);
	}
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
		PrepareRuntimeTextContent(Node->Content),
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
				PrepareRuntimeTextContent(Node->Content),
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
