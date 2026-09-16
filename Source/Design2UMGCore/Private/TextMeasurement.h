// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

/** Text fitting and measurement facade used by converter text paths. */
namespace TextMeasurement
{
	/** Return whether the node should use wrapping text behavior. */
	bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	/** Decide whether explicit line height should be applied for the node. */
	bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node);
	/** Resolve the UMG runtime font size for a design text node. */
	float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	/** Resolve the UMG wrap width for a design text node. */
	float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Return whether AutoWrapText should be enabled. */
	bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	/** Estimate runtime text size for layout conversion. */
	FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	/** Normalize source text for runtime UMG display. */
	FString PrepareRuntimeTextContent(const FString& Text);
}
