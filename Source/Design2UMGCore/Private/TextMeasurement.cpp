// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"
#include "TextMeasurementInternal.h"
#include "DesignSchemaSourceExtensions.h"

#include "Design2UMGLog.h"
#include "SharedCoreSemantics.h"
#include "TextStyleUtils.h"
#include "TextMeasurementRuntimeProbe.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
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
	return RuntimeProbeSizing::ResolveTextFontSizeForUMGInternal(Node, nullptr);
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

	if (RuntimeProbeSizing::ShouldUseSingleLineTextHeight(Node, FallbackSize.X, ParentContentWidth))
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
