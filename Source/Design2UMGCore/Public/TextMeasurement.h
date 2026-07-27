// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

/** Text fitting and measurement facade used by converter text paths. */
namespace TextMeasurement
{
	/** Return whether the node should use wrapping text behavior. */
	DESIGN2UMGCORE_API bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	/** Decide whether explicit line height should be applied for the node. */
	DESIGN2UMGCORE_API bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f);
	/** Resolve the UMG runtime font size for a design text node. */
	DESIGN2UMGCORE_API float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	/** Resolve the UMG wrap width for a design text node. */
	DESIGN2UMGCORE_API float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Return whether AutoWrapText should be enabled. */
	DESIGN2UMGCORE_API bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	/** Estimate runtime text size for layout conversion. */
	DESIGN2UMGCORE_API FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Normalize source text for runtime UMG display. */
	DESIGN2UMGCORE_API FString PrepareRuntimeTextContent(const FString& Text);
}
