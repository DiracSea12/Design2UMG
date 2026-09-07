// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"
#include "TextMeasurementInternal.h"
#include "DesignSchema.h"

#include "DesignNodeSemantics.h"
#include "TextStyleUtils.h"
#include "ParserSharedSemantics.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::DesignNodeSemantics::ResolveWidthHint(Node, ParentContentWidth);
	}

	bool IsTruncateTextNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		return Node->Text.bTruncateOverflow;
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
	if (!Node.IsValid())
	{
		return false;
	}

	if (IsTruncateTextNode(Node))
	{
		return false;
	}

	return ParserSharedSemantics::HasFixedWidthTextGrowth(*Node)
		|| ParserSharedSemantics::HasFillWidthAutoTextGrowth(*Node);
}

bool TextMeasurement::ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth)
{
	if (!Node.IsValid() || !FMath::IsFinite(Node->Text.LineHeight) || Node->Text.LineHeight <= 0.0f)
	{
		return false;
	}

	if (Node->Text.Content.Contains(TEXT("\n")))
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
	const int32 LetterSpacingTracking = (EffectiveProbeFontSize > 0.0f
		&& Node->Text.LetterSpacing.IsSet()
		&& Node->Text.LetterSpacing.GetValue() != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing.GetValue(), Node->Text.FontSize, EffectiveProbeFontSize)
		: 0;
	const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
		Node->Text.Content,
		EffectiveProbeFontSize,
		Node->Text.FontFamily,
		Node->Text.FontPostScriptName,
		Node->Text.FontWeight,
		Node->Text.FontStyle,
		LetterSpacingTracking);
	return UnwrappedSize.X > WidthLimit;
}

FResolvedTextLayoutForUMG TextMeasurementInternal::ResolveTextLayoutForUMG(
	const TSharedPtr<FDesignNode>& Node,
	const float ParentContentWidth)
{
	const float WrapTextAt = TextMeasurement::ResolveTextWrapAt(Node, ParentContentWidth);
	return RuntimeProbeSizing::ResolveTextLayoutForUMGInternal(Node, WrapTextAt);
}

float TextMeasurement::ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node)
{
	return TextMeasurementInternal::ResolveTextLayoutForUMG(Node).FontSize;
}

float TextMeasurement::ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}

	if (IsTruncateTextNode(Node))
	{
		return 0.0f;
	}

	const float WidthLimit = ResolveTextWidthHint(Node, ParentContentWidth);
	if (WidthLimit <= 0.0f)
	{
		return 0.0f;
	}

	const bool bNeedsExplicitWrap = ParserSharedSemantics::HasFixedWidthTextGrowth(*Node)
		|| ParserSharedSemantics::HasFillWidthAutoTextGrowth(*Node)
		|| ParserSharedSemantics::HasHardNewlineText(*Node);
	return bNeedsExplicitWrap ? WidthLimit : 0.0f;
}

bool TextMeasurement::ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node)
{
	return IsWrappingTextMode(Node);
}

namespace
{
	FVector2D MeasureResolvedTextNodeDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		const FResolvedTextLayoutForUMG& ResolvedLayout)
	{
		return RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			ResolvedLayout.FontSize,
			TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content),
			ResolvedLayout.WrapTextAt,
			TextMeasurement::ShouldAutoWrapText(Node),
			ResolvedLayout.LineHeightPercentage);
	}
}

FVector2D TextMeasurement::MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	if (!Node.IsValid())
	{
		return FVector2D::ZeroVector;
	}

	const FResolvedTextLayoutForUMG ResolvedLayout = TextMeasurementInternal::ResolveTextLayoutForUMG(Node, ParentContentWidth);
	const float AppliedFontSize = ResolvedLayout.FontSize;
	if (!FMath::IsFinite(AppliedFontSize) || AppliedFontSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}
	const int32 LetterSpacingTracking = (AppliedFontSize > 0.0f && Node->Text.LetterSpacing.IsSet() && Node->Text.LetterSpacing.GetValue() != 0.0f)
		? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing.GetValue(), Node->Text.FontSize, AppliedFontSize)
		: 0;
	const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
		PrepareRuntimeTextContent(Node->Text.Content),
		AppliedFontSize,
		Node->Text.FontFamily,
		Node->Text.FontPostScriptName,
		Node->Text.FontWeight,
		Node->Text.FontStyle,
		LetterSpacingTracking);

	const float WrapTextAt = ResolvedLayout.WrapTextAt;
	if (WrapTextAt <= 0.0f && !Node->Text.Content.Contains(TEXT("\n")))
	{
		return UnwrappedSize;
	}

	const FVector2D WrappedSize = MeasureResolvedTextNodeDesiredSize(Node, ResolvedLayout);
	if (WrappedSize.X > 0.0f && WrappedSize.Y > 0.0f)
	{
		return WrappedSize;
	}

	return FVector2D::ZeroVector;
}
