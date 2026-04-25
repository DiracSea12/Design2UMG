// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeStepGraph.h"
#include "Design2UMGPrototypeStepGraphAssembly.h"
#include "DesignSchemaSourceExtensions.h"

namespace
{
	int32 ResolvePrototypeInitialPageIndex(
		const FDesignDocument& Document,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex)
	{
		auto ResolvePageIndexForNodeId = [&](const FString& NodeId) -> int32
		{
			if (NodeId.IsEmpty())
			{
				return INDEX_NONE;
			}

			const FString* PageId = NodeToPageId.Find(NodeId);
			if (!PageId)
			{
				return INDEX_NONE;
			}

			const int32* PageIndex = PageIdToIndex.Find(*PageId);
			return PageIndex ? *PageIndex : INDEX_NONE;
		};

		if (const int32 StartPageIndex =
			ResolvePageIndexForNodeId(Document.SourceDocument().PrototypeStartNodeId);
			StartPageIndex != INDEX_NONE)
		{
			return StartPageIndex;
		}

		for (const FDesignFlowStartingPoint& Flow : Document.SourceDocument().FlowStartingPoints)
		{
			if (const int32 FlowPageIndex = ResolvePageIndexForNodeId(Flow.NodeId);
				FlowPageIndex != INDEX_NONE)
			{
				return FlowPageIndex;
			}
		}

		return 0;
	}
} // namespace

namespace Design2UMGPrototypeStepGraph
{
	Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis BuildAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings)
	{
		Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis Analysis;

		TArray<TSharedPtr<FDesignNode>> PrototypePages;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (Child.IsValid() && !Child->bReusable)
			{
				PrototypePages.Add(Child);
			}
		}

		if (PrototypePages.Num() == 0)
		{
			return Analysis;
		}

		Design2UMGPrototypeStepGraphAssembly::FPrototypeRoutingContext RoutingContext = Design2UMGPrototypeStepGraphAssembly::BuildRoutingContext(PrototypePages);
		if (PrototypePages.Num() > 1)
		{
			Analysis.Data.PageIds = RoutingContext.PageIds;
			Analysis.Data.SwitcherWidgetId = Design2UMGPrototypeStepGraphAssembly::MakeUniquePrototypeInternalToken(
				RoutingContext.ReservedTokens,
				RoutingContext.InternalTokenHash,
				TEXT("switcher"));
			Analysis.Data.SwitcherWidgetName = Analysis.Data.SwitcherWidgetId;
			RoutingContext.ReservedTokens.Add(Analysis.Data.SwitcherWidgetId);
		}

		TSet<FString> SeenBindingKeys;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			GatherBindings(
				PageNode,
				RoutingContext.NodeToPageId,
				RoutingContext.PageIdToIndex,
				Analysis.Data.ClickBindings,
				Analysis.TriggerNodeIds,
				SeenBindingKeys,
				Warnings);
		}

		if (Analysis.Data.HasOverlayBindings())
		{
			Analysis.Data.OverlayHostWidgetId = Design2UMGPrototypeStepGraphAssembly::MakeUniquePrototypeInternalToken(
				RoutingContext.ReservedTokens,
				RoutingContext.InternalTokenHash,
				TEXT("overlay_host"));
			Analysis.Data.OverlayHostWidgetName = Analysis.Data.OverlayHostWidgetId;
		}

		if (Analysis.Data.ClickBindings.Num() == 0)
		{
			Analysis.Data = FUMGPrototypeNavigationData();
			Analysis.TriggerNodeIds.Reset();
			return Analysis;
		}

		if (Analysis.Data.RequiresSwitcher() && Analysis.Data.PageIds.Num() < 2)
		{
			Analysis.Data = FUMGPrototypeNavigationData();
			Analysis.TriggerNodeIds.Reset();
			return Analysis;
		}

		if (Analysis.Data.RequiresSwitcher())
		{
			Analysis.Data.InitialPageIndex = ResolvePrototypeInitialPageIndex(
				Document,
				RoutingContext.NodeToPageId,
				RoutingContext.PageIdToIndex);
		}

		return Analysis;
	}

	void GatherBindings(
		const TSharedPtr<FDesignNode>& RootNode,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationBinding>& OutBindings,
		TSet<FString>& OutTriggerIds,
		TSet<FString>& InOutSeenBindingKeys,
		TArray<FString>& Warnings)
	{
		Design2UMGPrototypeStepGraphAssembly::GatherNavigationBindings(
			RootNode,
			NodeToPageId,
			PageIdToIndex,
			OutBindings,
			OutTriggerIds,
			InOutSeenBindingKeys,
			Warnings);
	}
}
