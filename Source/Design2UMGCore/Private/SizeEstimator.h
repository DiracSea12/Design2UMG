#pragma once

#include "DesignSchema.h"

namespace SizeEstimator
{
	void GatherVisibleChildren(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren);
	float ComputeChildContentWidthHint(const TSharedPtr<FDesignNode>& Node, const TSharedPtr<FDesignNode>& Child, float ParentContentWidth);
	void SetResolvedNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels);
	void PromoteFitNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels);
	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);
	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);
	FVector2D PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc);
}
