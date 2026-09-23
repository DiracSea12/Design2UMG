// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigation.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "Design2UMGPrototypeStepGraph.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"

namespace
{
	void ResetPrototypePageForSwitcher(FUMGWidgetNode& PageWidget)
	{
		PageWidget.Name = PageWidget.Id.IsEmpty() ? PageWidget.Name : PageWidget.Id;
		PageWidget.Bounds.X = 0.0f;
		PageWidget.Bounds.Y = 0.0f;
		PageWidget.Slot = FUMGSlotData();
	}

	/** Hosted-page conventions for a page parented under the overlay host at build time. */
	void ApplyHostedPageConventions(FUMGWidgetNode& PageWidget)
	{
		PageWidget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		PageWidget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		PageWidget.WidgetStyle.Visibility = TEXT("Collapsed");
	}

	/**
	 * Clone a page for the overlay host, clear design identity and append OverlayMirrorSuffix.
	 * Keep source metadata, including component mapping. The non-numeric suffix excludes
	 * mirrors from normal base-name matching while keeping them addressable by graph wiring.
	 */
	void MirrorWidgetSubtreeForOverlay(FUMGWidgetNode& Node)
	{
		Node.ClearSourceDesignIdentity();
		Node.Id += Design2UMGPrototypeTokens::OverlayMirrorSuffix();
		Node.Name += Design2UMGPrototypeTokens::OverlayMirrorSuffix();
		for (FUMGWidgetNode& Child : Node.Children)
		{
			MirrorWidgetSubtreeForOverlay(Child);
		}
	}

	/**
	 * Collect targets for one action kind from executable steps and binding summaries.
	 */
	TSet<FString> CollectActionTargetPageIds(
		const FUMGPrototypeNavigationData& NavigationData,
		EUMGPrototypeNavigationStepKind Kind)
	{
		const EUMGPrototypeNavigationAction BindingAction =
			Kind == EUMGPrototypeNavigationStepKind::Overlay
				? EUMGPrototypeNavigationAction::Overlay
				: EUMGPrototypeNavigationAction::Navigate;

		TSet<FString> Targets;
		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
		{
			if (Binding.Steps.Num() > 0)
			{
				for (const FUMGPrototypeNavigationStep& Step : Binding.Steps)
				{
					if (Step.Kind == Kind)
					{
						Targets.Add(Step.TargetPageId);
					}
				}
			}
			else if (Binding.Action == BindingAction)
			{
				Targets.Add(Binding.TargetPageId);
			}
		}
		return Targets;
	}
}

namespace Design2UMGPrototypeNavigation
{
	FPrototypeNavigationAnalysis BuildAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings)
	{
		return Design2UMGPrototypeStepGraph::BuildAnalysis(
			Document,
			VisibleChildren,
			Warnings);
	}

	bool ShouldWrapNodeWithButton(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGPrototypeNavigationData& NavigationData,
		const TSet<FString>& TriggerNodeIds)
	{
		return Node.IsValid()
			&& NavigationData.IsValid()
			&& TriggerNodeIds.Contains(Node->Id);
	}

	FUMGWidgetNode WrapWithButton(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Button;
		Button.Type = UMGWidgetTypes::Button;
		Button.Id = Node->Id;
		Button.Name = Node->Id.IsEmpty() ? TEXT("PrototypeButton") : Node->Id;
		Button.Bounds = Widget.Bounds;
		Button.Slot = Widget.Slot;
		Button.WidgetStyle.BackgroundColor = FLinearColor::Transparent;
		Button.WidgetStyle.HoveredColor = FLinearColor::Transparent;
		Button.WidgetStyle.PressedColor = FLinearColor::Transparent;

		Widget.Id = Node->Id + Design2UMGGeneratedNameSuffixes::Content();
		Widget.Name = (Node->Name.IsEmpty() ? Node->Id : Node->Name) + Design2UMGGeneratedNameSuffixes::Content();
		Widget.Bounds.X = 0.0f;
		Widget.Bounds.Y = 0.0f;
		Widget.Slot = FUMGSlotData();
		Button.Children.Add(MoveTemp(Widget));
		return Button;
	}

	FUMGWidgetNode BuildRoot(
		TArray<FUMGWidgetNode>&& PageWidgets,
		const FUMGPrototypeNavigationData& NavigationData)
	{
		// Overlay-target pages are parented under the overlay host at build time (Collapsed, Fill):
		// the runtime wiring then only toggles visibility, never reparents. A page that is also a
		// Navigate target must stay a switcher child (the switcher owns the page flow), and the initial
		// page must stay a switcher child too (InitialPageIndex keeps pointing at it); such pages get a
		// synthesized mirror copy under the host instead, so the overlay binding stays a layered display
		// over the current page and never disturbs the page flow or the history.
		const TSet<FString> OverlayPageIds = CollectActionTargetPageIds(
			NavigationData, EUMGPrototypeNavigationStepKind::Overlay);
		const TSet<FString> NavigatePageIds = CollectActionTargetPageIds(
			NavigationData, EUMGPrototypeNavigationStepKind::Navigate);

		FUMGWidgetNode BaseSwitcher;
		BaseSwitcher.Type = UMGWidgetTypes::WidgetSwitcher;
		BaseSwitcher.Id = NavigationData.SwitcherWidgetId;
		BaseSwitcher.Name = NavigationData.SwitcherWidgetName;

		float MaxWidth = 0.0f;
		float MaxHeight = 0.0f;
		int32 SwitcherChildCount = 0;
		TArray<FUMGWidgetNode> OverlayPages;
		const int32 InitialPagePosition = NavigationData.InitialPageIndex;
		for (int32 PageIndex = 0; PageIndex < PageWidgets.Num(); ++PageIndex)
		{
			FUMGWidgetNode& PageWidget = PageWidgets[PageIndex];
			ResetPrototypePageForSwitcher(PageWidget);
			// The design size is the max over ALL pages: an overlay page larger than the flow
			// pages must not shrink the switcher by moving under the host.
			MaxWidth = FMath::Max(MaxWidth, PageWidget.Bounds.W);
			MaxHeight = FMath::Max(MaxHeight, PageWidget.Bounds.H);

			const bool bOverlayTargetPage = OverlayPageIds.Contains(PageWidget.Id);
			const bool bOverlayOnlyPage = bOverlayTargetPage
				&& !NavigatePageIds.Contains(PageWidget.Id)
				&& PageIndex != InitialPagePosition;
			if (bOverlayOnlyPage)
			{
				ApplyHostedPageConventions(PageWidget);
				OverlayPages.Add(MoveTemp(PageWidget));
				continue;
			}
			if (PageIndex == InitialPagePosition)
			{
				BaseSwitcher.WidgetState.ActiveWidgetIndex = SwitcherChildCount;
			}
			++SwitcherChildCount;
			if (bOverlayTargetPage)
			{
				FUMGWidgetNode Mirror = PageWidget; // deep copy; children are value types
				ApplyHostedPageConventions(Mirror);
				MirrorWidgetSubtreeForOverlay(Mirror);
				OverlayPages.Add(MoveTemp(Mirror));
			}
			BaseSwitcher.Children.Add(MoveTemp(PageWidget));
		}

		BaseSwitcher.Bounds.W = MaxWidth;
		BaseSwitcher.Bounds.H = MaxHeight;
		if (!NavigationData.HasOverlayBindings())
		{
			return BaseSwitcher;
		}

		FUMGWidgetNode OverlayHost;
		OverlayHost.Type = UMGWidgetTypes::Overlay;
		OverlayHost.Id = NavigationData.OverlayHostWidgetId;
		OverlayHost.Name = NavigationData.OverlayHostWidgetName;
		OverlayHost.Bounds = BaseSwitcher.Bounds;
		OverlayHost.Slot = FUMGSlotData();
		OverlayHost.WidgetStyle.Visibility = TEXT("Collapsed");
		OverlayHost.Children = MoveTemp(OverlayPages);

		BaseSwitcher.Bounds.X = 0.0f;
		BaseSwitcher.Bounds.Y = 0.0f;
		BaseSwitcher.Slot = FUMGSlotData();
		BaseSwitcher.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		BaseSwitcher.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::Overlay;
		Root.Id = NavigationData.SwitcherWidgetId.IsEmpty()
			? TEXT("__d2u_prototype_root")
			: NavigationData.SwitcherWidgetId + Design2UMGPrototypeTokens::OverlayRootSuffix();
		Root.Name = NavigationData.SwitcherWidgetName.IsEmpty()
			? Root.Id
			: NavigationData.SwitcherWidgetName + Design2UMGPrototypeTokens::OverlayRootSuffix();
		Root.Bounds = BaseSwitcher.Bounds;
		Root.Children.Add(MoveTemp(BaseSwitcher));
		Root.Children.Add(MoveTemp(OverlayHost));
		return Root;
	}
}
