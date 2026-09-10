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

	/** TargetPageId set of one action kind across all bindings (steps are the executable shape; the binding-level fields cover the legacy no-steps export). */
	TSet<FString> CollectActionTargetPageIds(
		const FUMGPrototypeNavigationData& NavigationData,
		EUMGPrototypeNavigationStepKind Kind)
	{
		const EUMGPrototypeNavigationAction LegacyAction =
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
			else if (Binding.Action == LegacyAction)
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
		// Navigate target must stay a switcher child (the switcher owns the page flow), and the
		// initial page must stay a switcher child too (InitialPageIndex keeps pointing at it); the
		// wiring presents such overlays as a page swap through the Navigate chain instead.
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

			const bool bOverlayPage = OverlayPageIds.Contains(PageWidget.Id)
				&& !NavigatePageIds.Contains(PageWidget.Id)
				&& PageIndex != InitialPagePosition;
			if (bOverlayPage)
			{
				PageWidget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
				PageWidget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
				PageWidget.WidgetStyle.Visibility = TEXT("Collapsed");
				OverlayPages.Add(MoveTemp(PageWidget));
				continue;
			}
			if (PageIndex == InitialPagePosition)
			{
				BaseSwitcher.WidgetState.ActiveWidgetIndex = SwitcherChildCount;
			}
			++SwitcherChildCount;
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
