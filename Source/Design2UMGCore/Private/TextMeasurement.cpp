// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurement.h"
#include "TextMeasurementInternal.h"
#include "DesignSchema.h"

#include "DesignNodeSemantics.h"
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

bool TextMeasurement::ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node)
{
	return Node.IsValid() && FMath::IsFinite(Node->Text.LineHeight) && Node->Text.LineHeight > 0.0f;
}

FResolvedTextLayoutForUMG TextMeasurementInternal::ResolveTextLayoutForUMG(
	const TSharedPtr<FDesignNode>& Node,
	const float ParentContentWidth,
	const FDesign2UMGFontMappings* FontMappings)
{
	const float WrapTextAt = TextMeasurement::ResolveTextWrapAt(Node, ParentContentWidth);
	return RuntimeProbeSizing::ResolveTextLayoutForUMGInternal(Node, WrapTextAt, FontMappings);
}

float TextMeasurement::ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node, const FDesign2UMGFontMappings* FontMappings)
{
	return TextMeasurementInternal::ResolveTextLayoutForUMG(Node, 0.0f, FontMappings).FontSize;
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
		const FResolvedTextLayoutForUMG& ResolvedLayout,
		const FDesign2UMGFontMappings* FontMappings)
	{
		return RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			ResolvedLayout.FontSize,
			TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content),
			ResolvedLayout.WrapTextAt,
			TextMeasurement::ShouldAutoWrapText(Node),
			ResolvedLayout.LineHeightPercentage, FontMappings);
	}
}

FVector2D TextMeasurement::MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth, const FDesign2UMGFontMappings* FontMappings)
{
	if (!Node.IsValid())
	{
		return FVector2D::ZeroVector;
	}

	const FResolvedTextLayoutForUMG ResolvedLayout = TextMeasurementInternal::ResolveTextLayoutForUMG(Node, ParentContentWidth, FontMappings);
	const float AppliedFontSize = ResolvedLayout.FontSize;
	if (!FMath::IsFinite(AppliedFontSize) || AppliedFontSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}
	// FontMeasure omits tracking; measure the shaped widget for every line count.
	const FVector2D MeasuredSize = MeasureResolvedTextNodeDesiredSize(Node, ResolvedLayout, FontMappings);
	if (MeasuredSize.X > 0.0f && MeasuredSize.Y > 0.0f)
	{
		return MeasuredSize;
	}

	return FVector2D::ZeroVector;
}
