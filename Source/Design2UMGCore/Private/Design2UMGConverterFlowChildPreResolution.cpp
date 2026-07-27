// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverterFlowChildPreResolution.h"

#include "Design2UMGLog.h"
#include "DesignVariableResolver.h"
#include "RefExpander.h"
#include "SizeEstimator.h"

namespace
{
	struct FChildSizeSnapshot
	{
		TSharedPtr<FDesignNode> Child;
		FDesignSize RawWidth;
		FDesignSize RawHeight;
		FDesignSize EffectiveWidth;
		FDesignSize EffectiveHeight;
		bool bUsedExpandedRef = false;
	};

	const TCHAR* ToSizeModeLogString(const EDesignSizeMode Mode)
	{
		switch (Mode)
		{
		case EDesignSizeMode::Fixed: return TEXT("fixed");
		case EDesignSizeMode::FillContainer: return TEXT("fill");
		case EDesignSizeMode::FitContent: return TEXT("fit");
		default: return TEXT("unknown");
		}
	}

	bool ShouldPreResolveLinearFlow(const TSharedPtr<FDesignNode>& Node, const FUMGWidgetNode& Widget)
	{
		return Node.IsValid()
			&& Widget.Bounds.W > 0.0f
			&& Widget.Bounds.H > 0.0f
			&& (Node->LayoutData.Layout == EDesignLayout::Horizontal || Node->LayoutData.Layout == EDesignLayout::Vertical);
	}

	struct FFlowLayoutSizingContext
	{
		float InnerW = 0.0f;
		float InnerH = 0.0f;
		float TotalGap = 0.0f;
		bool bHorizontal = false;
	};

	struct FFlowChildSizingSummary
	{
		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
	};

	FFlowLayoutSizingContext BuildFlowLayoutSizingContext(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGWidgetNode& Widget,
		const int32 ChildCount)
	{
		FFlowLayoutSizingContext SizingContext;
		SizingContext.InnerW = Widget.Bounds.W - Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left - Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right;
		SizingContext.InnerH = Widget.Bounds.H - Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top - Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom;
		SizingContext.bHorizontal = Node->LayoutData.Layout == EDesignLayout::Horizontal;
		SizingContext.TotalGap = ChildCount > 1 ? Node->LayoutData.ExplicitGap.Get(0.0f) * (ChildCount - 1) : 0.0f;
		return SizingContext;
	}

	TArray<FChildSizeSnapshot> BuildFlowChildSnapshots(
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver)
	{
		TArray<FChildSizeSnapshot> Snapshots;
		Snapshots.Reserve(VisibleChildren.Num());
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			FChildSizeSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
			Snapshot.Child = Child;
			if (!Child.IsValid())
			{
				continue;
			}

			Snapshot.RawWidth = Child->Transform.Width;
			Snapshot.RawHeight = Child->Transform.Height;
			Snapshot.EffectiveWidth = Child->Transform.Width;
			Snapshot.EffectiveHeight = Child->Transform.Height;

			const TSharedPtr<FDesignNode> ExpandedChild = PreResolutionResolver.ExpandRef(Child);
			if (ExpandedChild != Child && ExpandedChild.IsValid())
			{
				Snapshot.bUsedExpandedRef = true;
				if (!Child->Transform.bHasWidth)
				{
					Snapshot.EffectiveWidth = ExpandedChild->Transform.Width;
				}
				if (!Child->Transform.bHasHeight)
				{
					Snapshot.EffectiveHeight = ExpandedChild->Transform.Height;
				}
			}
		}
		return Snapshots;
	}

	FFlowChildSizingSummary CollectFlowChildSizingSummary(
		const TArray<FChildSizeSnapshot>& ChildSnapshots,
		const bool bHorizontal)
	{
		FFlowChildSizingSummary Summary;
		for (const FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
		{
			const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
			if (MainSize.IsFill())
			{
				++Summary.FillCount;
			}
			else
			{
				Summary.FixedMainSum += MainSize.Value;
			}

			const FDesignSize& RawMain = bHorizontal ? ChildSnapshot.RawWidth : ChildSnapshot.RawHeight;
			const FDesignSize& RawCross = bHorizontal ? ChildSnapshot.RawHeight : ChildSnapshot.RawWidth;
			const FDesignSize& EffectiveCross = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
			UE_LOG(LogDesign2UMG, Verbose, TEXT("PreResolveFill Child '%s': ref=%d rawMain=%s(%.0f) effMain=%s(%.0f) rawCross=%s(%.0f) effCross=%s(%.0f)"),
				ChildSnapshot.Child.IsValid() ? *ChildSnapshot.Child->Id : TEXT("<invalid>"),
				ChildSnapshot.bUsedExpandedRef ? 1 : 0,
				ToSizeModeLogString(RawMain.Mode), RawMain.Value,
				ToSizeModeLogString(MainSize.Mode), MainSize.Value,
				ToSizeModeLogString(RawCross.Mode), RawCross.Value,
				ToSizeModeLogString(EffectiveCross.Mode), EffectiveCross.Value);
		}
		return Summary;
	}

	void ApplyFlowCrossFillSizing(FChildSizeSnapshot& ChildSnapshot, const FFlowLayoutSizingContext& SizingContext)
	{
		const FDesignSize& CrossSize = SizingContext.bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
		if (CrossSize.IsFill())
		{
			const bool bKeepSourceDimension = SizingContext.bHorizontal
				? ChildSnapshot.Child->Transform.bSourceHasHeight
				: ChildSnapshot.Child->Transform.bSourceHasWidth;
			SizeEstimator::SetResolvedNodeDimension(
				ChildSnapshot.Child,
				!SizingContext.bHorizontal,
				SizingContext.bHorizontal ? SizingContext.InnerH : SizingContext.InnerW,
				bKeepSourceDimension);
		}
	}

	void ApplyFlowFillSizing(
		const FUMGWidgetNode& Widget,
		TArray<FChildSizeSnapshot>& ChildSnapshots,
		const FFlowLayoutSizingContext& SizingContext,
		const FFlowChildSizingSummary& Summary)
	{
		if (Summary.FillCount <= 0)
		{
			for (FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
			{
				ApplyFlowCrossFillSizing(ChildSnapshot, SizingContext);
			}
			return;
		}

		const float AvailableMain =
			(SizingContext.bHorizontal ? SizingContext.InnerW : SizingContext.InnerH)
			- Summary.FixedMainSum
			- SizingContext.TotalGap;
		const float FillMain = FMath::Max(0.0f, AvailableMain / Summary.FillCount);
		for (FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
		{
			const FDesignSize& MainSize = SizingContext.bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
			if (MainSize.IsFill())
			{
				const bool bKeepSourceDimension = SizingContext.bHorizontal
					? ChildSnapshot.Child->Transform.bSourceHasWidth
					: ChildSnapshot.Child->Transform.bSourceHasHeight;
				SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, SizingContext.bHorizontal, FillMain, bKeepSourceDimension);
			}

			ApplyFlowCrossFillSizing(ChildSnapshot, SizingContext);
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
			*Widget.Name,
			SizingContext.bHorizontal ? TEXT("H") : TEXT("V"),
			SizingContext.InnerW,
			SizingContext.InnerH,
			Summary.FixedMainSum,
			Summary.FillCount,
			FillMain);
	}

	void PreResolveLinearFlowFillChildren(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGWidgetNode& Widget,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver)
	{
		if (!ShouldPreResolveLinearFlow(Node, Widget))
		{
			return;
		}

		TArray<FChildSizeSnapshot> ChildSnapshots = BuildFlowChildSnapshots(VisibleChildren, PreResolutionResolver);
		const FFlowLayoutSizingContext SizingContext =
			BuildFlowLayoutSizingContext(Node, Widget, ChildSnapshots.Num());
		const FFlowChildSizingSummary Summary =
			CollectFlowChildSizingSummary(ChildSnapshots, SizingContext.bHorizontal);
		ApplyFlowFillSizing(Widget, ChildSnapshots, SizingContext, Summary);
	}

	void PreResolveFlowFitChild(
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& Child,
		const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver)
	{
		if (!Child.IsValid() || (!Child->Transform.Width.IsFit() && !Child->Transform.Height.IsFit()))
		{
			return;
		}

		const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(ParentNode, Child, 0.0f);
		const TSharedPtr<FDesignNode> EstimateChild = PreResolutionResolver.ResolveExpandedChild(Child, false);
		const FVector2D Estimate = SizeEstimator::EstimateNodeSize(EstimateChild, ChildContentWidth);
		SizeEstimator::PromoteFitNodeDimension(Child, true, Estimate.X);
		SizeEstimator::PromoteFitNodeDimension(Child, false, Estimate.Y);
		if (Estimate.X > 0.0f || Estimate.Y > 0.0f)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
				*Child->Name, Estimate.X, Estimate.Y);
		}
	}
}

namespace Design2UMGConverterInternal
{
	FFlowChildPreResolutionResolver::FFlowChildPreResolutionResolver(
		const FDesignDocument& InDocument,
		const TMap<FString, FString>& InThemeContext)
		: Document(InDocument)
		, ThemeContext(InThemeContext)
	{
	}

	TSharedPtr<FDesignNode> FFlowChildPreResolutionResolver::ExpandRef(const TSharedPtr<FDesignNode>& Node) const
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ref)
		{
			return Node;
		}

		TSharedPtr<FDesignNode> Expanded = RefExpander::ExpandRef(Node, Document);
		if (!Expanded.IsValid())
		{
			return nullptr;
		}

		if (ThemeContext.Num() > 0)
		{
			FDesignVariableResolver Resolver(Document.Variables);
			Resolver.SetThemeContext(ThemeContext);
			RefExpander::ReThemeNode(Expanded, Resolver);
		}

		return Expanded;
	}

	TSharedPtr<FDesignNode> FFlowChildPreResolutionResolver::ResolveExpandedChild(
		const TSharedPtr<FDesignNode>& Child,
		const bool bPreComputeFitContent) const
	{
		TSharedPtr<FDesignNode> ExpandedChild = ExpandRef(Child);
		if (!ExpandedChild.IsValid())
		{
			return Child;
		}

		if (bPreComputeFitContent && ExpandedChild != Child)
		{
			SizeEstimator::PreComputeFitContentSizes(ExpandedChild, Document);
		}

		return ExpandedChild;
	}

	FFlowChildPreResolution::FFlowChildPreResolution(const FFlowChildPreResolutionResolver& InPreResolutionResolver)
		: PreResolutionResolver(InPreResolutionResolver)
	{
	}

	void FFlowChildPreResolution::PreResolveFlowChildren(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGWidgetNode& Widget,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
	{
		PreResolveLinearFlowFillChildren(Node, Widget, VisibleChildren, PreResolutionResolver);
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			PreResolveFlowFitChild(Node, Child, PreResolutionResolver);
		}
	}
}
