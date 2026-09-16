// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"
#include "FontResolver.h"

namespace SizeEstimator
{
	using FRefExpansionResolver = TFunctionRef<TSharedPtr<FDesignNode>(const TSharedPtr<FDesignNode>&)>;

	void GatherVisibleChildren(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren);
	float ComputeChildContentWidthHint(const TSharedPtr<FDesignNode>& Node, const TSharedPtr<FDesignNode>& Child, float ParentContentWidth);
	void SetResolvedNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels, bool bKeepSourceDimension = false);
	void PromoteFitNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels);
	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f, const FDesign2UMGFontMappings* FontMappings = nullptr);
	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f, const FDesign2UMGFontMappings* FontMappings = nullptr);

	/**
	 * Mutating promotion pass: resolves fit-content sizes (expanding Refs through the
	 * resolver) and WRITES the results back into node transform fields (Width/Height
	 * values plus the bHasWidth/bHasHeight/bSourceHasWidth/bSourceHasHeight flags;
	 * see FDesignTransformNodeData). Conversion output depends on this pass running
	 * before child conversion, so conversion call sites invoke it as a pre-pass on the
	 * working-copy IR.
	 */
	FVector2D PromoteFitContentSizes(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		FRefExpansionResolver RefResolver,
		const FDesign2UMGFontMappings* FontMappings = nullptr);
}
