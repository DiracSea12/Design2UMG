// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverterFlowChildPreResolution.h"

#include "Design2UMGConverterInternal.h"
#include "Design2UMGLog.h"
#include "DesignVariableResolver.h"
#include "RefExpander.h"
#include "SizeEstimator.h"

namespace
{
	FString BuildThemeSignature(const TMap<FString, FString>& ThemeContext)
	{
		if (ThemeContext.Num() == 0)
		{
			return FString();
		}

		TArray<FString> Keys;
		ThemeContext.GetKeys(Keys);
		Keys.Sort();

		FString Signature;
		for (const FString& Key : Keys)
		{
			Signature += FString::Printf(TEXT("%d:%s=%d:%s;"),
				Key.Len(), *Key,
				ThemeContext.FindChecked(Key).Len(), *ThemeContext.FindChecked(Key));
		}
		return Signature;
	}

	const FDesignNode* ResolveSnapshotNode(
		const Design2UMGConverterInternal::FRefExpansionCache& Cache,
		const FDesignNode* Node)
	{
		if (const FDesignNode* const* SnapshotNode = Cache.SnapshotAliases.Find(Node))
		{
			return *SnapshotNode;
		}
		return Node;
	}

	void RegisterSnapshotAliases(
		const TSharedPtr<FDesignNode>& Snapshot,
		const TSharedPtr<FDesignNode>& WorkingCopy,
		TMap<const FDesignNode*, const FDesignNode*>& Aliases)
	{
		if (!Snapshot.IsValid() || !WorkingCopy.IsValid())
		{
			return;
		}
		if (WorkingCopy->Type == EDesignNodeType::Ref)
		{
			Aliases.Add(WorkingCopy.Get(), Snapshot.Get());
		}
		const int32 ChildCount = FMath::Min(Snapshot->Children.Num(), WorkingCopy->Children.Num());
		for (int32 Index = 0; Index < ChildCount; ++Index)
		{
			RegisterSnapshotAliases(Snapshot->Children[Index], WorkingCopy->Children[Index], Aliases);
		}
		const auto RegisterPatchAliases = [&Aliases](const FDesignNodePatch& Source, const FDesignNodePatch& Copy)
		{
			if (!Source.Children.IsSet() || !Copy.Children.IsSet()) return;
			const int32 Count = FMath::Min(Source.Children->Num(), Copy.Children->Num());
			for (int32 Index = 0; Index < Count; ++Index)
				RegisterSnapshotAliases((*Source.Children)[Index], (*Copy.Children)[Index], Aliases);
		};
		if (Snapshot->RefOverrides.IsValid() && WorkingCopy->RefOverrides.IsValid())
			RegisterPatchAliases(*Snapshot->RefOverrides, *WorkingCopy->RefOverrides);
		for (const auto& Pair : Snapshot->Descendants)
		{
			if (const FDesignNodeOverride* WorkingDescendant = WorkingCopy->Descendants.Find(Pair.Key))
			{
				RegisterSnapshotAliases(Pair.Value.Replacement, WorkingDescendant->Replacement, Aliases);
				RegisterPatchAliases(Pair.Value.Patch, WorkingDescendant->Patch);
			}
		}
	}

	void ApplyLatestInstanceSizeOverrides(
		const TSharedPtr<FDesignNode>& RefNode,
		const TSharedPtr<FDesignNode>& Expanded)
	{
		if (!RefNode.IsValid() || !Expanded.IsValid())
		{
			return;
		}

		if (RefNode->Transform.Width.Value > 0.0f)
		{
			Expanded->Transform.Width = RefNode->Transform.Width;
			Expanded->Transform.bHasWidth = true;
		}
		if (RefNode->Transform.Height.Value > 0.0f)
		{
			Expanded->Transform.Height = RefNode->Transform.Height;
			Expanded->Transform.bHasHeight = true;
		}
	}

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
			// Raw refs may still have the default Fit mode. Resolve the inherited
			// mode together with the pixels, or later expansion loses Fill.
			FDesignSize& ResolvedSize = SizingContext.bHorizontal
				? ChildSnapshot.Child->Transform.Height : ChildSnapshot.Child->Transform.Width;
			ResolvedSize.Mode = CrossSize.Mode;
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
				FDesignSize& ResolvedSize = SizingContext.bHorizontal
					? ChildSnapshot.Child->Transform.Width : ChildSnapshot.Child->Transform.Height;
				ResolvedSize.Mode = MainSize.Mode;
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
		TSharedPtr<FDesignNode> EstimateChild = Child;
		const TSharedPtr<FDesignNode> Snapshot = PreResolutionResolver.ExpandRef(Child);
		if (Snapshot.IsValid() && Snapshot != Child)
		{
			// Estimation only reads descendants; isolate the root's current instance dimensions.
			EstimateChild = MakeShared<FDesignNode>(*Snapshot);
			ApplyLatestInstanceSizeOverrides(Child, EstimateChild);
		}
		const FVector2D Estimate = SizeEstimator::EstimateNodeSize(EstimateChild, ChildContentWidth, PreResolutionResolver.GetFontMappings());
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
	namespace
	{
		void PropagateExpansionOutcome(
			const FDesignReferenceExpansionResult& Result,
			FRefExpansionCache& Cache)
		{
			if (Result.IsSuccess() || Result.Message.IsEmpty())
			{
				return;
			}

			if (Result.IsFatal())
			{
				if (Cache.FatalError && Cache.FatalError->IsEmpty())
				{
					*Cache.FatalError = Result.Message;
				}
				return;
			}

			if (Cache.Warnings)
			{
				const int32* Index = Cache.WarningIndices.Find(Result.Message);
				if (!Index || !Cache.Warnings->IsValidIndex(*Index) || (*Cache.Warnings)[*Index] != Result.Message)
				{
					Cache.WarningIndices.Add(Result.Message, Cache.Warnings->Add(Result.Message));
				}
			}
		}
	}

	FFlowChildPreResolutionResolver::FFlowChildPreResolutionResolver(
		const FDesignDocument& InDocument,
		const TMap<FString, FString>& InThemeContext,
		TSharedPtr<FRefExpansionCache> InExpansionCache,
		const FDesign2UMGFontMappings* InFontMappings,
		const FDesign2UMGConverter::FPrepareFonts& InPrepareFonts)
		: Document(InDocument)
		, ThemeContext(InThemeContext)
		, ExpansionCache(InExpansionCache.IsValid() ? MoveTemp(InExpansionCache) : MakeShared<FRefExpansionCache>(InDocument))
		, FontMappings(InFontMappings)
		, PrepareFonts(InPrepareFonts)
	{
	}

	TSharedPtr<FDesignNode> FFlowChildPreResolutionResolver::ExpandRef(const TSharedPtr<FDesignNode>& Node) const
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ref)
		{
			return Node;
		}

		const FDesignNode* CacheNode = ResolveSnapshotNode(*ExpansionCache, Node.Get());
		const FRefExpansionCacheKey CacheKey{ CacheNode, BuildThemeSignature(ThemeContext) };
		if (FRefExpansionCacheEntry* Cached = ExpansionCache->Entries.Find(CacheKey))
		{
			PropagateExpansionOutcome(Cached->Result, *ExpansionCache);
			return Cached->Result.ExpandedNode;
		}

		FRefExpansionCacheEntry& CacheEntry = ExpansionCache->Entries.Add(CacheKey);
		CacheEntry.Result = RefExpander::ExpandRef(Node, ExpansionCache->ReferenceTargetsById);
		PropagateExpansionOutcome(CacheEntry.Result, *ExpansionCache);
		if (!CacheEntry.Result.IsSuccess())
		{
			return nullptr;
		}
		TSharedPtr<FDesignNode>& Expanded = CacheEntry.Result.ExpandedNode;

		if (ThemeContext.Num() > 0)
		{
			FDesignVariableResolver Resolver(Document.Variables);
			Resolver.SetThemeContext(ThemeContext);
			RefExpander::ReThemeNode(Expanded, Resolver);
		}

		if (PrepareFonts)
		{
			// Instance text and font variables are final here, before any fit-content measurement.
			FDesignDocument ExpandedDocument;
			ExpandedDocument.Fonts = Document.Fonts;
			ExpandedDocument.Children.Add(Expanded);
			TArray<FString> LocalWarnings;
			TArray<FString>& Warnings = ExpansionCache->Warnings ? *ExpansionCache->Warnings : LocalWarnings;
			FString Error;
			const bool bPrepared = PrepareFonts(ExpandedDocument, Warnings, Error, FDesign2UMGConverter::EFontPreparation::GlyphCoverage);
			if (!bPrepared)
			{
				if (ExpansionCache->FatalError) *ExpansionCache->FatalError = Error;
				Expanded.Reset();
				return nullptr;
			}
		}
		return Expanded;
	}

	TSharedPtr<FDesignNode> FFlowChildPreResolutionResolver::MakeWorkingCopy(
		const TSharedPtr<FDesignNode>& RefNode) const
	{
		TSharedPtr<FDesignNode> Snapshot = ExpandRef(RefNode);
		if (!Snapshot.IsValid() || Snapshot == RefNode)
		{
			return Snapshot;
		}

		TSharedPtr<FDesignNode> WorkingCopy = RefExpander::DeepCopyNode(Snapshot);
		RegisterSnapshotAliases(Snapshot, WorkingCopy, ExpansionCache->SnapshotAliases);
		ApplyLatestInstanceSizeOverrides(RefNode, WorkingCopy);
		return WorkingCopy;
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
			auto RefResolver =
				[this](const TSharedPtr<FDesignNode>& RefNode)
				{
					return MakeWorkingCopy(RefNode);
				};
			// Fit estimates depend on the current layout context (including
			// available parent width), so reuse the Expanded snapshot but let
			// each caller measure it for its own context.
			TSharedPtr<FDesignNode> WorkingExpanded = MakeWorkingCopy(Child);
			SizeEstimator::PromoteFitContentSizes(
				WorkingExpanded,
				Document,
				RefResolver,
				FontMappings,
				ExpansionCache->FatalError);
			return WorkingExpanded;
		}

		return ExpandedChild == Child ? Child : MakeWorkingCopy(Child);
	}

void FFlowChildPreResolutionResolver::PreResolveFlowChildren(
	const TSharedPtr<FDesignNode>& Node,
	const FUMGWidgetNode& Widget,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
{
	PreResolveLinearFlowFillChildren(Node, Widget, VisibleChildren, *this);
	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		PreResolveFlowFitChild(Node, Child, *this);
	}
}
}
