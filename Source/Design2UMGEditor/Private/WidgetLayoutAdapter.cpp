// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "WidgetLayoutAdapter.h"
#include "UMGLayoutProjection.h"
#include "UMGValueParsing.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"

bool FWidgetLayoutAdapter::IsRootCanvasChild(UPanelWidget* ParentWidget, UWidgetTree* WidgetTree)
{
	return Cast<UCanvasPanel>(ParentWidget) != nullptr
		&& WidgetTree != nullptr
		&& WidgetTree->RootWidget == ParentWidget;
}

FWidgetLayoutAdapter::FNonDefaultGroups FWidgetLayoutAdapter::GetNonDefaultSlotGroups(const FUMGSlotData& Slot)
{
	const FUMGSlotData Default;
	FNonDefaultGroups Groups;
	Groups.bCanvas =
		!FMath::IsNearlyEqual(Slot.Anchors.Min.X, Default.Anchors.Min.X) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Default.Anchors.Min.Y) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.X, Default.Anchors.Max.X) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.Y, Default.Anchors.Max.Y) ||
		!FMath::IsNearlyEqual(Slot.Alignment.X, Default.Alignment.X) ||
		!FMath::IsNearlyEqual(Slot.Alignment.Y, Default.Alignment.Y) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Left, Default.Offsets.Left) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Top, Default.Offsets.Top) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Right, Default.Offsets.Right) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Bottom, Default.Offsets.Bottom);
	Groups.bPadding = !FUMGValueParser::IsNearlyEqualMargin(Slot.Padding, Default.Padding);
	Groups.bBox =
		Slot.HorizontalAlignment != Default.HorizontalAlignment ||
		Slot.VerticalAlignment != Default.VerticalAlignment ||
		Slot.SizeRule != Default.SizeRule ||
		!FMath::IsNearlyEqual(Slot.FillWeight, Default.FillWeight);
	Groups.bGrid =
		Slot.Row != Default.Row ||
		Slot.Column != Default.Column ||
		Slot.RowSpan != Default.RowSpan ||
		Slot.ColumnSpan != Default.ColumnSpan;
	return Groups;
}

bool FWidgetLayoutAdapter::HasMeaningfulSlot(const FUMGSlotData& Slot)
{
	const FNonDefaultGroups Groups = GetNonDefaultSlotGroups(Slot);
	return Groups.bCanvas || Groups.bPadding || Groups.bBox || Groups.bGrid;
}

FUMGSlotData FWidgetLayoutAdapter::Resolve(
	const FUMGWidgetNode& Node,
	UPanelWidget* ParentWidget,
	UWidgetTree* WidgetTree,
	const FIntPoint& DesignSize,
	bool bEnableAdaptiveAnchors)
{
	FUMGSlotData SlotData = Node.Slot;

	if (!IsRootCanvasChild(ParentWidget, WidgetTree))
	{
		return SlotData;
	}

	if (!bEnableAdaptiveAnchors || HasMeaningfulSlot(SlotData))
	{
		return SlotData;
	}

	if (Node.Bounds.W <= 0.0f || Node.Bounds.H <= 0.0f)
	{
		return SlotData;
	}

	FLayoutContext Context;
	Context.DesignSize = DesignSize;


	FDesignPixelBounds NormalizedBounds;
	NormalizedBounds.X = Node.Bounds.X / DesignSize.X;
	NormalizedBounds.Y = Node.Bounds.Y / DesignSize.Y;
	NormalizedBounds.W = Node.Bounds.W / DesignSize.X;
	NormalizedBounds.H = Node.Bounds.H / DesignSize.Y;

	const bool bOrigAutoSize = SlotData.bAutoSize;
	SlotData = LayoutCalculator::CalculateSlot(NormalizedBounds, Context);
	SlotData.bAutoSize = bOrigAutoSize;

	return SlotData;
}
