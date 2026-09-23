// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "TextFontResolution.h"
#include "TextMeasurement.h"
#include "TextMeasurementRuntimeProbe.h"
#include "Design2UMGFontMetrics.h"

namespace RuntimeProbeSizing
{
namespace
{
	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	TOptional<float> ResolvePreferredFontSize(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return {};
		}

		return TextFontResolution::ResolveSourceFontSizeForUMG(Node->Text.FontSize);
	}

	/** Single-line height the runtime would give this text without an explicit line height. */
	bool TryMeasureNaturalLineHeight(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize,
		float& OutNaturalLineHeight,
		const FDesign2UMGFontMappings* FontMappings)
	{
		FString SingleLineText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content);
		SingleLineText.ReplaceInline(TEXT("\n"), TEXT(" "));
		const FVector2D NaturalLineSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node, FontSize, SingleLineText, 0.0f, false, {}, FontMappings);
		if (!IsFinitePositive(NaturalLineSize.Y))
		{
			return false;
		}

		OutNaturalLineHeight = NaturalLineSize.Y;
		return true;
	}

	/** Reads the actual resolved face's line gap, including engine fonts. */
	bool TryResolveHheaLineGapPx(const TSharedPtr<FDesignNode>& Node, float& OutLineGapPx, const FDesign2UMGFontMappings* FontMappings)
	{
		OutLineGapPx = 0.0f;
		const TextFontResolution::FResolvedFontSelection ResolvedFont = TextFontResolution::Resolve(
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight,
			Node->Text.FontStyle, FontMappings);
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.FontAssetPath = ResolvedFont.FontAssetPath;
		Request.TypefaceName = ResolvedFont.TypefaceName;
		float LineGapEm = 0.0f;
		if (!Design2UMGFontMetrics::TryReadLineGapEm(
			TextMeasurementRuntimeProbe::BuildFontInfo(Request), LineGapEm))
		{
			return false;
		}

		// Font sizes are authored in source px and converted to points at 96 DPI,
		// which Slate converts back to the same pixel count, so the source font
		// size is also the runtime pixel size of the font.
		OutLineGapPx = LineGapEm * Node->Text.FontSize;
		return true;
	}
}

FResolvedTextLayoutForUMG ResolveTextLayoutForUMGInternal(
	const TSharedPtr<FDesignNode>& Node,
	const float WrapTextAt,
	const FDesign2UMGFontMappings* FontMappings)
{
	FResolvedTextLayoutForUMG Result;
	Result.WrapTextAt = WrapTextAt;

	const TOptional<float> PreferredFontSizeResult = ResolvePreferredFontSize(Node);
	if (!PreferredFontSizeResult.IsSet())
	{
		return Result;
	}

	const float PreferredFontSize = PreferredFontSizeResult.GetValue();
	Result.FontSize = PreferredFontSize;

	// Line advance follows authored line height. First-line translation also needs
	// the parser's source half-leading flag; ascent-top sources keep their baseline.
	float NaturalLineHeight = 0.0f;
	float SourceLineHeight = Node.IsValid() ? Node->Text.FontSize * Node->Text.LineHeight : 0.0f;
	if (Node.IsValid() && Node->Text.bSourceRoundLineHeightToPixels)
	{
		SourceLineHeight = FMath::RoundToFloat(SourceLineHeight);
	}
	const bool bHasSourceLineHeight = TextMeasurement::ShouldApplyExplicitLineHeight(Node)
		&& IsFinitePositive(SourceLineHeight);
	if (bHasSourceLineHeight
		&& TryMeasureNaturalLineHeight(Node, Result.FontSize, NaturalLineHeight, FontMappings))
	{
		Result.LineHeightPercentage = SourceLineHeight / NaturalLineHeight;

		float LineGapPx = 0.0f;
		if (Node->Text.bSourceHalfLeadingBaseline
			&& TryResolveHheaLineGapPx(Node, LineGapPx, FontMappings))
		{
			FString SingleLineText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content);
			SingleLineText.ReplaceInline(TEXT("\n"), TEXT(" "));
			// Baselines use font ascent/descent, not UTextBlock's shadow-expanded desired size.
			const float NaturalFontHeight = RuntimeProbe::MeasureTextSize(SingleLineText, Result.FontSize,
				Node->Text.FontFamily, Node->Text.FontPostScriptName, Node->Text.FontWeight, Node->Text.FontStyle, 0, FontMappings).Y;
			// Align Slate's natural-ascent baseline to the source editor's half-leading baseline.
			// Shift upward by (NaturalLineHeight - SourceLineHeight + hheaLineGap) / 2.
			// Keep subpixel precision to preserve half-leading placement.
			if (IsFinitePositive(NaturalFontHeight))
			{
				Result.LineHeightBaselineShift = -((NaturalFontHeight - SourceLineHeight) / 2.0f + LineGapPx / 2.0f);
			}
		}
	}
	return Result;
}
}
