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
				UE_LOG(LogDesign2UMG, Error, TEXT("Design node nesting exceeded %d levels; a cyclic reference is likely present."), SizeEstimatorMaxTreeRecursionDepth);
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

		if (!Node->Visual.Fill.IsNone() && Node->Visual.Fill.bEnabled)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (!Fill.IsNone() && Fill.bEnabled)
			{
				return true;
			}
		}

		if (Node->Visual.Stroke.HasStroke())
		{
			return true;
		}

		for (const FDesignEffect& Effect : Node->Visual.Effects)
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
		if (!Node.IsValid() || !Node->Transform.bEnabled || IsDesignerOnlyNodeTypeForSizing(Node->Type))
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
				&& Node->LayoutData.Layout != EDesignLayout::None;
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

		if (Node->Transform.Width.IsFill())
		{
			return Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : ParentContentWidth;
		}

		return Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : 0.0f;
	}

	bool HasExplicitFitDimension(const FDesignSize& Size)
	{
		return Size.IsFit() && Size.Value > 0.0f;
	}

	bool ShouldLockExplicitFitDimensionForSizing(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Children.Num() == 0
			&& (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			&& !Node->Text.Content.IsEmpty();
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

		if (!Node.IsValid() || !Node->Transform.bEnabled)
		{
			return FVector2D::ZeroVector;
		}

		if (Node->Type == EDesignNodeType::Ref)
		{
			if (TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Doc))
			{
				const FVector2D RefSize = PreComputeFitContentSizesImpl(Expanded, Doc, ParentContentWidth, State);
				const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForSizing(Node);
				if (Node->Transform.Width.IsFit() && !(bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Width)) && RefSize.X > Node->Transform.Width.Value && !Expanded->Transform.Width.IsFill())
				{
					Node->Transform.Width.Value = static_cast<float>(RefSize.X);
				}
				if (Node->Transform.Height.IsFit() && !(bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Height)) && RefSize.Y > Node->Transform.Height.Value && !Expanded->Transform.Height.IsFill())
				{
					Node->Transform.Height.Value = static_cast<float>(RefSize.Y);
				}
				return RefSize;
			}
			return FVector2D::ZeroVector;
		}

		if (Node->Type == EDesignNodeType::Text)
		{
			float W = Node->Transform.Width.Value;
			float H = Node->Transform.Height.Value;
			const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForSizing(Node);
			const bool bHasExplicitFitWidth = bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Width);
			const bool bHasExplicitFitHeight = bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Height);
			if (Node->Transform.Width.IsFill())
			{
				const float FillWidth = ResolveSizingWidthHint(Node, ParentContentWidth);
				if (FillWidth > 0.0f)
				{
					W = FMath::Max(W, FillWidth);
				}
			}

			const bool bNeedWidthEstimate = !Node->Transform.Width.IsFixed() && W <= 0.0f;
			const bool bNeedHeightEstimate = !Node->Transform.Height.IsFixed() && H <= 0.0f;
			if ((bNeedWidthEstimate || bNeedHeightEstimate || Node->Transform.Width.IsFit() || Node->Transform.Height.IsFit())
				&& Node->Text.FontSize > 0
				&& !Node->Text.Content.IsEmpty())
			{
				const FVector2D Measured = TextMeasurement::MeasureTextNodeSize(Node, ParentContentWidth);
				if (!bHasExplicitFitWidth)
				{
					MergeEstimatedDimension(W, Node->Transform.Width, Measured.X, bNeedWidthEstimate);
				}
				if (!bHasExplicitFitHeight)
				{
					MergeEstimatedDimension(H, Node->Transform.Height, Measured.Y, bNeedHeightEstimate);
				}
			}
			else if (bNeedHeightEstimate && Node->Text.FontSize > 0)
			{
				H = Node->Text.FontSize * 1.5f;
			}

			if (!bHasExplicitFitWidth)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, true, W);
			}
			if (!bHasExplicitFitHeight)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, false, H);
			}
			return FVector2D(W, H);
		}

		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->Text.FontSize > 0 ? Node->Text.FontSize : 16.0f;
			float W = Node->Transform.Width.Value;
			float H = Node->Transform.Height.Value;
			const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForSizing(Node);
			const bool bHasExplicitFitWidth = bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Width);
			const bool bHasExplicitFitHeight = bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Height);
			if (!bHasExplicitFitWidth)
			{
				MergeEstimatedDimension(W, Node->Transform.Width, S, !Node->Transform.Width.IsFixed() && W <= 0.0f);
				SizeEstimator::PromoteFitNodeDimension(Node, true, W);
			}
			if (!bHasExplicitFitHeight)
			{
				MergeEstimatedDimension(H, Node->Transform.Height, S, !Node->Transform.Height.IsFixed() && H <= 0.0f);
				SizeEstimator::PromoteFitNodeDimension(Node, false, H);
			}
			return FVector2D(W, H);
		}

		if (IsVectorLeaf(Node))
		{
			const float W = FMath::Max(1.0f, FMath::RoundToFloat(Node->Transform.Width.Value));
			const float H = FMath::Max(1.0f, FMath::RoundToFloat(Node->Transform.Height.Value));
			if (!Node->Transform.Width.IsFixed() || Node->Transform.Width.Value < 1.0f)
			{
				SizeEstimator::SetResolvedNodeDimension(Node, true, W);
			}
			if (!Node->Transform.Height.IsFixed() || Node->Transform.Height.Value < 1.0f)
			{
				SizeEstimator::SetResolvedNodeDimension(Node, false, H);
			}
			return FVector2D(W, H);
		}

		if (!Node->IsContainer() && Node->Type != EDesignNodeType::Rectangle)
		{
			return FVector2D(
				Node->Transform.Width.IsFixed() ? Node->Transform.Width.Value : 0.0f,
				Node->Transform.Height.IsFixed() ? Node->Transform.Height.Value : 0.0f);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
			(void)PreComputeFitContentSizesImpl(Child, Doc, ChildContentWidth, State);
		}

		float FinalW = Node->Transform.Width.Value;
		float FinalH = Node->Transform.Height.Value;
		if (Node->Transform.Width.IsFit() || Node->Transform.Height.IsFit())
		{
			const FVector2D Estimated = EstimateContentSizeImpl(Node, ParentContentWidth, State);
			const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForSizing(Node);
			if (Node->Transform.Width.IsFit() && !(bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Width)) && Estimated.X > Node->Transform.Width.Value)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, true, Estimated.X);
				FinalW = Node->Transform.Width.Value;
				UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' width fit→%.0f"), *Node->Name, Estimated.X);
			}
			if (Node->Transform.Height.IsFit() && !(bLockExplicitFit && HasExplicitFitDimension(Node->Transform.Height)) && Estimated.Y > Node->Transform.Height.Value)
			{
				SizeEstimator::PromoteFitNodeDimension(Node, false, Estimated.Y);
				FinalH = Node->Transform.Height.Value;
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

	if (Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
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

	const float InnerWidth = FMath::Max(0.0f, ResolvedWidth - Node->LayoutData.Padding.Left - Node->LayoutData.Padding.Right);
	if (InnerWidth <= 0.0f)
	{
		return 0.0f;
	}

	if (Node->LayoutData.Layout != EDesignLayout::Horizontal || VisibleChildren.Num() == 1)
	{
		return InnerWidth;
	}

	float RemainingWidth = InnerWidth - Node->LayoutData.Gap * (VisibleChildren.Num() - 1);
	int32 FillChildCount = 0;
	for (const TSharedPtr<FDesignNode>& VisibleChild : VisibleChildren)
	{
		if (VisibleChild->Transform.Width.IsFill())
		{
			++FillChildCount;
			continue;
		}

		RemainingWidth -= ResolveSizingWidthHint(VisibleChild, 0.0f);
	}

	if (!Child->Transform.Width.IsFill() || FillChildCount <= 0 || RemainingWidth <= 0.0f)
	{
		return InnerWidth;
	}

	return RemainingWidth / FillChildCount;
}

void SizeEstimator::SetResolvedNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels)
{
	FDesignSize& Size = bWidth ? Node->Transform.Width : Node->Transform.Height;
	Size.Value = FMath::Max(0.0f, Pixels);
	if (bWidth)
	{
		Node->Transform.bHasWidth = true;
	}
	else
	{
		Node->Transform.bHasHeight = true;
	}
}

void SizeEstimator::PromoteFitNodeDimension(const TSharedPtr<FDesignNode>& Node, bool bWidth, float Pixels)
{
	FDesignSize& Size = bWidth ? Node->Transform.Width : Node->Transform.Height;
	if (!Size.IsFit() || Pixels <= Size.Value)
	{
		return;
	}

	Size.Value = Pixels;
	if (bWidth)
	{
		Node->Transform.bHasWidth = true;
	}
	else
	{
		Node->Transform.bHasHeight = true;
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

		float W = Node->Transform.Width.Value;
		float H = Node->Transform.Height.Value;
		if (Node->Transform.Width.IsFill())
		{
			const float FillWidth = ResolveSizingWidthHint(Node, ParentContentWidth);
			if (FillWidth > 0.0f)
			{
				W = FMath::Max(W, FillWidth);
			}
		}

		if (Node->Type == EDesignNodeType::Text)
		{
			if ((W == 0 || H == 0 || Node->Transform.Width.IsFit() || Node->Transform.Height.IsFit())
				&& Node->Text.FontSize > 0
				&& !Node->Text.Content.IsEmpty())
			{
				const FVector2D Measured = TextMeasurement::MeasureTextNodeSize(Node, ParentContentWidth);
				MergeEstimatedDimension(W, Node->Transform.Width, Measured.X);
				MergeEstimatedDimension(H, Node->Transform.Height, Measured.Y);
			}
			else if (H == 0 && Node->Text.FontSize > 0)
			{
				H = Node->Text.FontSize * 1.5f;
			}
			return FVector2D(W, H);
		}

		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->Text.FontSize > 0 ? Node->Text.FontSize : 16.0f;
			MergeEstimatedDimension(W, Node->Transform.Width, S);
			MergeEstimatedDimension(H, Node->Transform.Height, S);
			return FVector2D(W, H);
		}

		if (Node->Children.Num() > 0)
		{
			const FVector2D Sub = EstimateContentSizeImpl(Node, ParentContentWidth, State);
			MergeEstimatedDimension(W, Node->Transform.Width, Sub.X);
			MergeEstimatedDimension(H, Node->Transform.Height, Sub.Y);
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
		const float PadH = Node->LayoutData.Padding.Left + Node->LayoutData.Padding.Right;
		const float PadV = Node->LayoutData.Padding.Top + Node->LayoutData.Padding.Bottom;

		TArray<TSharedPtr<FDesignNode>> Visible;
		SizeEstimator::GatherVisibleChildren(Node, Visible);
		if (Visible.Num() == 0)
		{
			return FVector2D(PadH, PadV);
		}

		const float TotalGap = Visible.Num() > 1 ? Node->LayoutData.Gap * (Visible.Num() - 1) : 0.0f;
		if (Node->LayoutData.Layout == EDesignLayout::Horizontal)
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
		else if (Node->LayoutData.Layout == EDesignLayout::Vertical)
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
				W = FMath::Max(W, Child->Transform.X + ChildSize.X);
				H = FMath::Max(H, Child->Transform.Y + ChildSize.Y);
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
