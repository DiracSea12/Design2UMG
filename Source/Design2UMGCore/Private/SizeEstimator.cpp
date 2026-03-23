// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "SizeEstimator.h"

#include "Design2UMGLog.h"
#include "RefExpander.h"
#include "TextMeasurement.h"

namespace
{
	constexpr int32 SizeEstimatorMaxTreeRecursionDepth = 512;

	struct FSizeEstimatorState
	{
		int32 EstimateDepth = 0;
		int32 PrecomputeDepth = 0;
	};

	struct FSizeEstimatorRecursionGuard
	{
		explicit FSizeEstimatorRecursionGuard(int32& InDepth, const TCHAR* InName)
			: Depth(InDepth)
			, bValid(InDepth < SizeEstimatorMaxTreeRecursionDepth)
		{
			if (bValid)
			{
				++Depth;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, SizeEstimatorMaxTreeRecursionDepth);
				UE_LOG(LogDesign2UMG, Error, TEXT("设计文件节点嵌套层级过深（超过 %d 层），可能存在循环引用。"), SizeEstimatorMaxTreeRecursionDepth);
			}
		}

		~FSizeEstimatorRecursionGuard()
		{
			if (bValid)
			{
				--Depth;
			}
		}

		int32& Depth;
		bool bValid = false;
	};

	bool IsDesignerOnlyNodeTypeForSizing(EDesignNodeType Type)
	{
		return Type == EDesignNodeType::Connection
			|| Type == EDesignNodeType::Note
			|| Type == EDesignNodeType::Prompt
			|| Type == EDesignNodeType::Context;
	}

	bool HasRenderableVisualForSizing(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
		{
			return true;
		}

		if (!Node->Fill.IsNone() && Node->Fill.bEnabled)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Fills)
		{
			if (!Fill.IsNone() && Fill.bEnabled)
			{
				return true;
			}
		}

		if (Node->Stroke.HasStroke())
		{
			return true;
		}

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled)
			{
				return true;
			}
		}

		return false;
	}

	bool ShouldSkipNodeForSizing(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->bEnabled || IsDesignerOnlyNodeTypeForSizing(Node->Type))
		{
			return true;
		}

		if (Node->Type == EDesignNodeType::Ref)
		{
			return false;
		}

		if (Node->Children.Num() == 0 && !HasRenderableVisualForSizing(Node))
		{
			const bool bIsExplicitFlowContainer =
				(Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
				&& Node->Layout != EDesignLayout::None;
			if (!bIsExplicitFlowContainer)
			{
				return true;
			}
		}

		return false;
	}

	float ResolveSizingWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		if (Node->Width.IsFill())
		{
			return Node->Width.Value > 0.0f ? Node->Width.Value : ParentContentWidth;
		}

		return Node->Width.Value > 0.0f ? Node->Width.Value : 0.0f;
	}

	void MergeEstimatedDimension(float& CurrentValue, const FDesignSize& Size, float Estimate, bool bForceEstimate = false)
	{
		if (bForceEstimate || CurrentValue <= 0.0f)
		{
			CurrentValue = Estimate;
		}
		else if (Size.IsFit())
		{
			CurrentValue = FMath::Max(CurrentValue, Estimate);
		}
	}

	bool IsVectorLeaf(const TSharedPtr<FDesignNode>& Node)
	{
		return Node->Type == EDesignNodeType::Path
			|| Node->Type == EDesignNodeType::Line
			|| Node->Type == EDesignNodeType::Ellipse
			|| Node->Type == EDesignNodeType::Polygon;
	}

	FVector2D EstimateNodeSizeImpl(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth, FSizeEstimatorState& State);
	FVector2D EstimateContentSizeImpl(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth, FSizeEstimatorState& State);

	FVector2D PreComputeFitContentSizesImpl(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		float ParentContentWidth,
		FSizeEstimatorState& State)
	{
		FSizeEstimatorRecursionGuard Guard(State.PrecomputeDepth, TEXT("PreComputeFitContentSizes"));
		if (!Guard.bValid)
		{
			return FVector2D::ZeroVector;
		}

		if (!Node.IsValid() || !Node->bEnabled)
		{
			return FVector2D::ZeroVector;
		}

		if (Node->Type == EDesignNodeType::Ref)
		{
			if (TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Doc))
			{
				const FVector2D RefSize = PreComputeFitContentSizesImpl(Expanded, Doc, ParentContentWidth, State);
				if (Node->Width.IsFit() && RefSize.X > Node->Width.Value && !Expanded->Width.IsFill())
				{
					Node->Width.Value = static_cast<float>(RefSize.X);
				}
				if (Node->Height.IsFit() && RefSize.Y > Node->Height.Value && !Expanded->Height.IsFill())
				{
					Node->Height.Value = static_cast<float>(RefSize.Y);
				}
				return RefSize;
			}
			return FVector2D::ZeroVector;
		}

		if (Node->Type == EDesignNodeType::Text)
		{
			float W = Node->Width.Value;
			float H = Node->Height.Value;
			if (Node->Width.IsFill())
			{
				const float FillWidth = ResolveSizingWidthHint(Node, ParentContentWidth);
				if (FillWidth > 0.0f)
				{
					W = FMath::Max(W, FillWidth);
				}
			}

			const bool bNeedWidthEstimate = !Node->Width.IsFixed() && W <= 0.0f;
			const bool bNeedHeightEstimate = !Node->Height.IsFixed() && H <= 0.0f;
			if ((bNeedWidthEstimate || bNeedHeightEstimate || Node->Width.IsFit() || Node->Height.IsFit())
				&& Node->FontSize > 0
				&& !Node->Content.IsEmpty())
			{
				const FVector2D Measured = TextMeasurement::MeasureTextNodeSize(Node, ParentContentWidth);
				MergeEstimatedDimension(W, Node->Width, Measured.X, bNeedWidthEstimate);
				MergeEstimatedDimension(H, Node->Height, Measured.Y, bNeedHeightEstimate);
			}
			else if (bNeedHeightEstimate && Node->FontSize > 0)
			{
				H = Node->FontSize * 1.5f;
			}

			SizeEstimator::PromoteFitNodeDimension(Node, true, W);
			SizeEstimator::PromoteFitNodeDimension(Node, false, H);
			return FVector2D(W, H);
		}

		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
			float W = Node->Width.Value;
			float H = Node->Height.Value;
			MergeEstimatedDimension(W, Node->Width, S, !Node->Width.IsFixed() && W <= 0.0f);
			MergeEstimatedDimension(H, Node->Height, S, !Node->Height.IsFixed() && H <= 0.0f);
			SizeEstimator::PromoteFitNodeDimension(Node, true, W);
			SizeEstimator::PromoteFitNodeDimension(Node, false, H);
			return FVector2D(W, H);
		}

		if (IsVectorLeaf(Node))
		{
			const float W = FMath::Max(1.0f, FMath::RoundToFloat(Node->Width.Value));
			const float H = FMath::Max(1.0f, FMath::RoundToFloat(Node->Height.Value));
			if (!Node->Width.IsFixed() || Node->Width.Value < 1.0f)
			{
				SizeEstimator::SetResolvedNodeDimension(Node, true, W);
			}
			if (!Node->Height.IsFixed() || Node->Height.Value < 1.0f)
			{
				SizeEstimator::SetResolvedNodeDimension(Node, false, H);
			}
			return FVector2D(W, H);
		}

		if (!Node->IsContainer() && Node->Type != EDesignNodeType::Rectangle)
		{
			return FVector2D(
				Node->Width.IsFixed() ? Node->Width.Value : 0.0f,
				Node->Height.IsFixed() ? Node->Height.Value : 0.0f);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
			(void)PreComputeFitContentSizesImpl(Child, Doc, ChildContentWidth, State);
		}

		float FinalW = Node->Width.Value;
		float FinalH = Node->Height.Value;
		if (Node->Width.IsFit() || Node->Height.IsFit())
		{
			const FVector2D Estimated = EstimateContentSizeImpl(Node, ParentContentWidth, State);
			if (Node->Width.IsFit() && Estimated.X > Node->Width.Value)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, true, Estimated.X);
				FinalW = Node->Width.Value;
				UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' width fit→%.0f"), *Node->Name, Estimated.X);
			}
			if (Node->Height.IsFit() && Estimated.Y > Node->Height.Value)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, false, Estimated.Y);
				FinalH = Node->Height.Value;
				UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' height fit→%.0f"), *Node->Name, Estimated.Y);
			}
		}

		return FVector2D(FinalW, FinalH);
	}
}

void SizeEstimator::GatherVisibleChildren(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren)
{
	if (!Node.IsValid())
	{
		return;
	}

	for (const TSharedPtr<FDesignNode>& Child : Node->Children)
	{
		if (!ShouldSkipNodeForSizing(Child))
		{
			OutVisibleChildren.Add(Child);
		}
	}
}

float SizeEstimator::ComputeChildContentWidthHint(const TSharedPtr<FDesignNode>& Node, const TSharedPtr<FDesignNode>& Child, float ParentContentWidth)
{
	if (!Node.IsValid() || !Child.IsValid())
	{
		return 0.0f;
	}

	if (Node->Layout == EDesignLayout::Wrap || Node->Layout == EDesignLayout::Grid)
	{
		return 0.0f;
	}

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	GatherVisibleChildren(Node, VisibleChildren);
	if (VisibleChildren.Num() == 0)
	{
		return 0.0f;
	}

	const float ResolvedWidth = ResolveSizingWidthHint(Node, ParentContentWidth);
	if (ResolvedWidth <= 0.0f)
	{
		return 0.0f;
	}

	const float InnerWidth = FMath::Max(0.0f, ResolvedWidth - Node->Padding.Left - Node->Padding.Right);
	if (InnerWidth <= 0.0f)
	{
		return 0.0f;
	}

	if (Node->Layout != EDesignLayout::Horizontal || VisibleChildren.Num() == 1)
	{
		return InnerWidth;
	}

	float RemainingWidth = InnerWidth - Node->Gap * (VisibleChildren.Num() - 1);
	int32 FillChildCount = 0;
	for (const TSharedPtr<FDesignNode>& VisibleChild : VisibleChildren)
	{
		if (VisibleChild->Width.IsFill())
		{
			++FillChildCount;
			continue;
		}

		RemainingWidth -= ResolveSizingWidthHint(VisibleChild, 0.0f);
	}

	if (!Child->Width.IsFill() || FillChildCount <= 0 || RemainingWidth <= 0.0f)
	{
		return InnerWidth;
	}

	return RemainingWidth / FillChildCount;
}

void SizeEstimator::SetResolvedNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels)
{
	FDesignSize& Size = bWidth ? Node->Width : Node->Height;
	Size.Value = FMath::Max(0.0f, Pixels);
	if (bWidth)
	{
		Node->bHasWidth = true;
	}
	else
	{
		Node->bHasHeight = true;
	}
}

void SizeEstimator::PromoteFitNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels)
{
	FDesignSize& Size = bWidth ? Node->Width : Node->Height;
	if (!Size.IsFit() || Pixels <= Size.Value)
	{
		return;
	}

	Size.Value = Pixels;
	if (bWidth)
	{
		Node->bHasWidth = true;
	}
	else
	{
		Node->bHasHeight = true;
	}
}

namespace
{
	FVector2D EstimateNodeSizeImpl(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth, FSizeEstimatorState& State)
	{
		FSizeEstimatorRecursionGuard Guard(State.EstimateDepth, TEXT("EstimateNodeSize"));
		if (!Guard.bValid)
		{
			return FVector2D::ZeroVector;
		}

		float W = Node->Width.Value;
		float H = Node->Height.Value;
		if (Node->Width.IsFill())
		{
			const float FillWidth = ResolveSizingWidthHint(Node, ParentContentWidth);
			if (FillWidth > 0.0f)
			{
				W = FMath::Max(W, FillWidth);
			}
		}

		if (Node->Type == EDesignNodeType::Text)
		{
			if ((W == 0 || H == 0 || Node->Width.IsFit() || Node->Height.IsFit())
				&& Node->FontSize > 0
				&& !Node->Content.IsEmpty())
			{
				const FVector2D Measured = TextMeasurement::MeasureTextNodeSize(Node, ParentContentWidth);
				MergeEstimatedDimension(W, Node->Width, Measured.X);
				MergeEstimatedDimension(H, Node->Height, Measured.Y);
			}
			else if (H == 0 && Node->FontSize > 0)
			{
				H = Node->FontSize * 1.5f;
			}
			return FVector2D(W, H);
		}

		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
			MergeEstimatedDimension(W, Node->Width, S);
			MergeEstimatedDimension(H, Node->Height, S);
			return FVector2D(W, H);
		}

		if (Node->Children.Num() > 0)
		{
			const FVector2D Sub = EstimateContentSizeImpl(Node, ParentContentWidth, State);
			MergeEstimatedDimension(W, Node->Width, Sub.X);
			MergeEstimatedDimension(H, Node->Height, Sub.Y);
		}

		return FVector2D(W, H);
	}

	FVector2D EstimateContentSizeImpl(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth, FSizeEstimatorState& State)
	{
		FSizeEstimatorRecursionGuard Guard(State.EstimateDepth, TEXT("EstimateContentSize"));
		if (!Guard.bValid)
		{
			return FVector2D::ZeroVector;
		}

		float W = 0.0f;
		float H = 0.0f;
		const float PadH = Node->Padding.Left + Node->Padding.Right;
		const float PadV = Node->Padding.Top + Node->Padding.Bottom;

		TArray<TSharedPtr<FDesignNode>> Visible;
		SizeEstimator::GatherVisibleChildren(Node, Visible);
		if (Visible.Num() == 0)
		{
			return FVector2D(PadH, PadV);
		}

		const float TotalGap = Visible.Num() > 1 ? Node->Gap * (Visible.Num() - 1) : 0.0f;
		if (Node->Layout == EDesignLayout::Horizontal)
		{
			for (const TSharedPtr<FDesignNode>& Child : Visible)
			{
				const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
				const FVector2D ChildSize = EstimateNodeSizeImpl(Child, ChildContentWidth, State);
				W += ChildSize.X;
				H = FMath::Max(H, ChildSize.Y);
			}
			W += TotalGap;
		}
		else if (Node->Layout == EDesignLayout::Vertical)
		{
			for (const TSharedPtr<FDesignNode>& Child : Visible)
			{
				const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
				const FVector2D ChildSize = EstimateNodeSizeImpl(Child, ChildContentWidth, State);
				W = FMath::Max(W, ChildSize.X);
				H += ChildSize.Y;
			}
			H += TotalGap;
		}
		else
		{
			for (const TSharedPtr<FDesignNode>& Child : Visible)
			{
				const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
				const FVector2D ChildSize = EstimateNodeSizeImpl(Child, ChildContentWidth, State);
				W = FMath::Max(W, Child->X + ChildSize.X);
				H = FMath::Max(H, Child->Y + ChildSize.Y);
			}
		}

		return FVector2D(W + PadH, H + PadV);
	}
}

FVector2D SizeEstimator::EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	FSizeEstimatorState State;
	return EstimateNodeSizeImpl(Node, ParentContentWidth, State);
}

FVector2D SizeEstimator::EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
{
	FSizeEstimatorState State;
	return EstimateContentSizeImpl(Node, ParentContentWidth, State);
}

FVector2D SizeEstimator::PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc)
{
	FSizeEstimatorState State;
	return PreComputeFitContentSizesImpl(Node, Doc, 0.0f, State);
}
