// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"

namespace SizeEstimator
{
	using FRefExpansionResolver = TFunctionRef<TSharedPtr<FDesignNode>(const TSharedPtr<FDesignNode>&)>;

	void GatherVisibleChildren(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren);
	float ComputeChildContentWidthHint(const TSharedPtr<FDesignNode>& Node, const TSharedPtr<FDesignNode>& Child, float ParentContentWidth);
	void SetResolvedNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels, bool bKeepSourceDimension = false);
	void PromoteFitNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels);
	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);
	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);
	FVector2D PreComputeFitContentSizes(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		FRefExpansionResolver RefResolver);
}
