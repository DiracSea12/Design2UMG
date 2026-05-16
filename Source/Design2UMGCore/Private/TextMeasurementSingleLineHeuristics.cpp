// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "SharedCoreSemantics.h"
#include "TextStyleUtils.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
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

namespace
{
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

	bool IsRuntimeProportionalTypeface(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Text.FontSize <= 0.0f)
		{
			return false;
		}

		const FVector2D NarrowGlyphRun = RuntimeProbe::MeasureTextSize(
			TEXT("iiii"),
			Node->Text.FontSize,
			Node->Text.FontFamily,
			Node->Text.FontWeight);
		const FVector2D WideGlyphRun = RuntimeProbe::MeasureTextSize(
			TEXT("WWWW"),
			Node->Text.FontSize,
			Node->Text.FontFamily,
			Node->Text.FontWeight);
		if (NarrowGlyphRun.X <= KINDA_SMALL_NUMBER || WideGlyphRun.X <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float NarrowerWidth = FMath::Min(NarrowGlyphRun.X, WideGlyphRun.X);
		const float WiderWidth = FMath::Max(NarrowGlyphRun.X, WideGlyphRun.X);
		return WiderWidth >= NarrowerWidth * 1.25f;
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
}

bool IsExpandedRefIntrinsicSingleLineText(const TSharedPtr<FDesignNode>& Node)
{
	return Node.IsValid()
		&& Node->Source().bExpandedRefInstance
		&& !Node->Text.Content.Contains(TEXT("\n"))
		&& !TextMeasurement::IsWrappingTextMode(Node)
		&& Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top
		&& !Node->Transform.bSourceHasWidth
		&& Node->Text.FontSize >= 12.0f
		&& !IsHeavyFontWeight(Node->Text.FontWeight);
}

bool IsDenseTechnicalTokenText(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Source().bExpandedRefInstance
		|| Node->Text.Content.Contains(TEXT("\n"))
		|| TextMeasurement::IsWrappingTextMode(Node)
		|| Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top
		|| Node->Transform.bSourceHasWidth
		|| IsHeavyFontWeight(Node->Text.FontWeight))
	{
		return false;
	}

	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content).TrimStartAndEnd();
	if (RuntimeText.Len() < 8 || !ContainsLetter(RuntimeText) || ContainsLowercaseLetter(RuntimeText))
	{
		return false;
	}

	if (RuntimeText.EndsWith(TEXT(":")))
	{
		return false;
	}

	bool bHasWhitespace = false;
	for (const TCHAR Char : RuntimeText)
	{
		if (FChar::IsWhitespace(Char))
		{
			bHasWhitespace = true;
			break;
		}
	}

	return !bHasWhitespace
		&& (ContainsMetricMetaSymbol(RuntimeText) || ContainsAsciiDigit(RuntimeText));
}

bool ShouldUseTightExpandedRefIntrinsicScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!IsExpandedRefIntrinsicSingleLineText(Node)
		|| Node->Transform.Height.Value < 17.5f)
	{
		return false;
	}

	return IsRuntimeProportionalTypeface(Node);
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

	const float DefaultFallbackFontSize =
		Node->Text.FontSize * FallbackCalibration::FindFamilySpecificTextFallbackScale(
			FallbackCalibration::NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily));
	if (DefaultFallbackFontSize <= 0.0f)
	{
		return false;
	}

	const int32 LetterSpacingTracking = Node->Text.LetterSpacing != 0.0f
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(
			Node->Text.LetterSpacing,
			Node->Text.FontSize,
			DefaultFallbackFontSize)
		: 0;
	const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
		Node->Text.Content,
		DefaultFallbackFontSize,
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
