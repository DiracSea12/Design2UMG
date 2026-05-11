// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigation.h"

#include "Design2UMGPrototypeStepGraph.h"
#include "UMGSchema.h"

namespace
{
	void ResetPrototypePageForSwitcher(FUMGWidgetNode& PageWidget)
	{
		PageWidget.Name = PageWidget.Id.IsEmpty() ? PageWidget.Name : PageWidget.Id;
		PageWidget.Bounds.X = 0.0f;
		PageWidget.Bounds.Y = 0.0f;
		PageWidget.Slot = FUMGSlotData();
	}

	FUMGWidgetNode BuildPrototypeSwitcherRoot(
		TArray<FUMGWidgetNode>&& PageWidgets,
		const FUMGPrototypeNavigationData& NavigationData)
	{
		FUMGWidgetNode Switcher;
		Switcher.Type = TEXT("WidgetSwitcher");
		Switcher.Id = NavigationData.SwitcherWidgetId;
		Switcher.Name = NavigationData.SwitcherWidgetName;
		Switcher.WidgetState.ActiveWidgetIndex = NavigationData.InitialPageIndex;

		float MaxWidth = 0.0f;
		float MaxHeight = 0.0f;
		for (FUMGWidgetNode& PageWidget : PageWidgets)
		{
			ResetPrototypePageForSwitcher(PageWidget);
			MaxWidth = FMath::Max(MaxWidth, PageWidget.Bounds.W);
			MaxHeight = FMath::Max(MaxHeight, PageWidget.Bounds.H);
			Switcher.Children.Add(MoveTemp(PageWidget));
		}

		Switcher.Bounds.W = MaxWidth;
		Switcher.Bounds.H = MaxHeight;
		return Switcher;
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
		Button.Type = TEXT("Button");
		Button.Id = Node->Id;
		Button.Name = Node->Id.IsEmpty() ? TEXT("PrototypeButton") : Node->Id;
		Button.Bounds = Widget.Bounds;
		Button.Slot = Widget.Slot;
		Button.WidgetStyle.BackgroundColor = FLinearColor::Transparent;
		Button.WidgetStyle.HoveredColor = FLinearColor::Transparent;
		Button.WidgetStyle.PressedColor = FLinearColor::Transparent;

		Widget.Id = Node->Id + TEXT("_content");
		Widget.Name = (Node->Name.IsEmpty() ? Node->Id : Node->Name) + TEXT("_content");
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
		FUMGWidgetNode BaseSwitcher = BuildPrototypeSwitcherRoot(MoveTemp(PageWidgets), NavigationData);
		if (!NavigationData.HasOverlayBindings())
		{
			return BaseSwitcher;
		}

		FUMGWidgetNode OverlayHost;
		OverlayHost.Type = TEXT("Overlay");
		OverlayHost.Id = NavigationData.OverlayHostWidgetId;
		OverlayHost.Name = NavigationData.OverlayHostWidgetName;
		OverlayHost.Bounds = BaseSwitcher.Bounds;
		OverlayHost.Slot = FUMGSlotData();
		OverlayHost.WidgetStyle.Visibility = TEXT("Collapsed");

		BaseSwitcher.Bounds.X = 0.0f;
		BaseSwitcher.Bounds.Y = 0.0f;
		BaseSwitcher.Slot = FUMGSlotData();
		BaseSwitcher.Slot.HorizontalAlignment = TEXT("Fill");
		BaseSwitcher.Slot.VerticalAlignment = TEXT("Fill");

		FUMGWidgetNode Root;
		Root.Type = TEXT("Overlay");
		Root.Id = NavigationData.SwitcherWidgetId.IsEmpty()
			? TEXT("__d2u_prototype_root")
			: NavigationData.SwitcherWidgetId + TEXT("_root");
		Root.Name = NavigationData.SwitcherWidgetName.IsEmpty()
			? Root.Id
			: NavigationData.SwitcherWidgetName + TEXT("_root");
		Root.Bounds = BaseSwitcher.Bounds;
		Root.Children.Add(MoveTemp(BaseSwitcher));
		Root.Children.Add(MoveTemp(OverlayHost));
		return Root;
	}
}
