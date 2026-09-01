// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "TextFontResolution.h"
#include "TextMeasurement.h"
#include "TextMeasurementRuntimeProbe.h"

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
		float& OutNaturalLineHeight)
	{
		FString SingleLineText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content);
		SingleLineText.ReplaceInline(TEXT("\n"), TEXT(" "));
		const FVector2D NaturalLineSize = RuntimeProbe::MeasureTextSize(
			SingleLineText,
			FontSize,
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight,
			Node->Text.FontStyle,
			0);
		if (!IsFinitePositive(NaturalLineSize.Y))
		{
			return false;
		}

		OutNaturalLineHeight = NaturalLineSize.Y;
		return true;
	}

	/**
	 * Resolves the font's hhea lineGap in source pixels. Returns false when the
	 * source font asset cannot be resolved (runtime fallback font) or when the
	 * resolution lands on an engine-bundled font: the baseline shift is only
	 * meaningful when the runtime font is the authored one, since fallback and
	 * engine-bundled font builds (e.g. the engine Roboto) do not share metrics
	 * with the reference renderer's font version.
	 */
	bool TryResolveHheaLineGapPx(const TSharedPtr<FDesignNode>& Node, float& OutLineGapPx)
	{
		OutLineGapPx = 0.0f;
		const TextFontResolution::FResolvedFontSelection ResolvedFont = TextFontResolution::Resolve(
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight,
			Node->Text.FontStyle);
		if (ResolvedFont.FontAssetPath.StartsWith(TEXT("/Engine/EngineFonts/"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		float LineGapEm = 0.0f;
		if (!TextMeasurementRuntimeProbe::TryReadLineGapEm(
				ResolvedFont.FontAssetPath,
				ResolvedFont.TypefaceName,
				LineGapEm))
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
	const float WrapTextAt)
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

	// The line-height percentage (line advance rescale) applies to every source
	// that declares a line height. The first-line baseline shift additionally
	// applies only to sources flagged as half-leading renderers (Figma): Pencil
	// keeps the natural ascent at the top of its line boxes, which already
	// matches Slate.
	float NaturalLineHeight = 0.0f;
	const float SourceLineHeight = Node.IsValid() ? Node->Text.FontSize * Node->Text.LineHeight : 0.0f;
	const bool bHasSourceLineHeight = Node.IsValid()
		&& FMath::IsFinite(Node->Text.LineHeight)
		&& Node->Text.LineHeight > 0.0f
		&& IsFinitePositive(SourceLineHeight);
	if (bHasSourceLineHeight
		&& TryMeasureNaturalLineHeight(Node, Result.FontSize, NaturalLineHeight))
	{
		if (TextMeasurement::ShouldApplyExplicitLineHeight(Node, Result.FontSize, WrapTextAt))
		{
			Result.LineHeightPercentage = SourceLineHeight / NaturalLineHeight;
		}

		float LineGapPx = 0.0f;
		if (Node->Text.bSourceHalfLeadingBaseline
			&& TryResolveHheaLineGapPx(Node, LineGapPx))
		{
			// Slate keeps the first-line baseline at the font's natural ascent no
			// matter how LineHeightPercentage rescales the line box (FTextLayout
			// places blocks by the unscaled MaxAboveBaseline). A half-leading
			// source editor instead centers the natural line box inside the
			// authored line height and baselines at the hhea ascent only, so the
			// Slate text renders lower by
			//   (NaturalLineHeight - SourceLineHeight) / 2 + hheaLineGap / 2
			// where the lineGap term exists because Slate's Metrics layout adds
				// the hhea lineGap on top of the ascent (FT_Face height = ascender -
				// descender + lineGap). The shift below moves the rendered text back
				// up by that amount. Whole-pixel rounding was evaluated and rejected:
				// the reference renderer places the baseline at sub-pixel half-leading
				// positions, so snapping the shift to whole pixels moves mid-way
				// cases further from the reference (course-builder measured worse).
				Result.LineHeightBaselineShift = -((NaturalLineHeight - SourceLineHeight) / 2.0f + LineGapPx / 2.0f);
		}
	}
	return Result;
}
}
