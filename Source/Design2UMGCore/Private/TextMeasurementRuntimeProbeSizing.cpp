// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "TextFontResolution.h"
#include "TextMeasurement.h"

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

	TOptional<float> ResolveLineHeightPercentage(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize)
	{
		if (!Node.IsValid() || !IsFinitePositive(FontSize))
		{
			return {};
		}
		FString SingleLineText = TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content);
		SingleLineText.ReplaceInline(TEXT("\n"), TEXT(" "));
		const FVector2D NaturalLineSize = RuntimeProbe::MeasureTextSize(
			SingleLineText,
			FontSize,
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight,
			0);
		const float SourceLineHeight = Node->Text.FontSize * Node->Text.LineHeight;
		if (!IsFinitePositive(NaturalLineSize.Y) || !IsFinitePositive(SourceLineHeight))
		{
			return {};
		}

		return SourceLineHeight / NaturalLineSize.Y;
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
	if (TextMeasurement::ShouldApplyExplicitLineHeight(Node, Result.FontSize, WrapTextAt))
	{
		Result.LineHeightPercentage = ResolveLineHeightPercentage(Node, Result.FontSize);
	}
	return Result;
}
}
