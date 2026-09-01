// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

struct FResolvedTextLayoutForUMG
{
	float FontSize = 0.0f;
	float WrapTextAt = 0.0f;
	TOptional<float> LineHeightPercentage;
	/**
	 * Vertical render offset (px, negative = up) that aligns the Slate first-line
	 * baseline placement with the source editor's half-leading placement when an
	 * explicit line height is applied. See RuntimeProbeSizing for the derivation.
	 */
	TOptional<float> LineHeightBaselineShift;
};

namespace TextMeasurementInternal
{
	FResolvedTextLayoutForUMG ResolveTextLayoutForUMG(
		const TSharedPtr<FDesignNode>& Node,
		float ParentContentWidth = 0.0f);
}

namespace RuntimeProbe
{
	FVector2D MeasureTextSize(
		const FString& Text,
		float FontSize,
		const FString& FontFamily,
		const FString& FontPostScriptName,
		const FString& FontWeight,
		const FString& FontStyle = FString(),
		int32 LetterSpacing = 0);
	FVector2D MeasureUMGTextBlockDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		float FontSize,
		const FString& Text,
		float WrapTextAt = 0.0f,
		bool bAutoWrapText = false,
		TOptional<float> LineHeightPercentageOverride = {});
}
namespace RuntimeProbeSizing
{
	FResolvedTextLayoutForUMG ResolveTextLayoutForUMGInternal(
		const TSharedPtr<FDesignNode>& Node,
		float WrapTextAt);
}
