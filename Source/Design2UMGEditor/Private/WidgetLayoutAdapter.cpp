// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "WidgetLayoutAdapter.h"
#include "LayoutCalculator.h"
#include "UMGValueParser.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"

bool FWidgetLayoutAdapter::IsRootCanvasChild(UPanelWidget* ParentWidget, UWidgetTree* WidgetTree)
{
	return Cast<UCanvasPanel>(ParentWidget) != nullptr
		&& WidgetTree != nullptr
		&& WidgetTree->RootWidget == ParentWidget;
}

bool FWidgetLayoutAdapter::HasMeaningfulSlot(const FUMGSlotData& Slot)
{
	const FUMGSlotData Default;
	const bool bAnchors =
		!FMath::IsNearlyEqual(Slot.Anchors.Min.X, Default.Anchors.Min.X) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Default.Anchors.Min.Y) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.X, Default.Anchors.Max.X) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.Y, Default.Anchors.Max.Y);
	const bool bAlignment =
		!FMath::IsNearlyEqual(Slot.Alignment.X, Default.Alignment.X) ||
		!FMath::IsNearlyEqual(Slot.Alignment.Y, Default.Alignment.Y);
	const bool bOffsets =
		!FMath::IsNearlyEqual(Slot.Offsets.Left, Default.Offsets.Left) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Top, Default.Offsets.Top) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Right, Default.Offsets.Right) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Bottom, Default.Offsets.Bottom);
	const bool bPadding = !FUMGValueParser::IsNearlyEqualMargin(Slot.Padding, Default.Padding);
	const bool bBox =
		Slot.HorizontalAlignment != Default.HorizontalAlignment ||
		Slot.VerticalAlignment != Default.VerticalAlignment ||
		Slot.SizeRule != Default.SizeRule ||
		!FMath::IsNearlyEqual(Slot.FillWeight, Default.FillWeight);
	const bool bGrid =
		Slot.Row != Default.Row ||
		Slot.Column != Default.Column ||
		Slot.RowSpan != Default.RowSpan ||
		Slot.ColumnSpan != Default.ColumnSpan;
	return bAnchors || bAlignment || bOffsets || bPadding || bBox || bGrid;
}

bool FWidgetLayoutAdapter::HasValidSlotHistory(const FUMGSlotData& Slot)
{
	return !FMath::IsNearlyEqual(Slot.Offsets.Left, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Top, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Right, 100.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Bottom, 100.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.Y, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.Y, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Alignment.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Alignment.Y, 0.0f);
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
	Context.Preset = FLayoutPreset::GetStandardHUDPreset();

	if (HasValidSlotHistory(Node.Slot))
	{
		if (Node.Slot.Anchors.Min.X == Node.Slot.Anchors.Max.X)
		{
			Context.OldAnchorX = Node.Slot.Anchors.Min.X;
		}
		if (Node.Slot.Anchors.Min.Y == Node.Slot.Anchors.Max.Y)
		{
			Context.OldAnchorY = Node.Slot.Anchors.Min.Y;
		}
	}

	FDesignPixelBounds NormalizedBounds;
	NormalizedBounds.X = Node.Bounds.X / DesignSize.X;
	NormalizedBounds.Y = Node.Bounds.Y / DesignSize.Y;
	NormalizedBounds.W = Node.Bounds.W / DesignSize.X;
	NormalizedBounds.H = Node.Bounds.H / DesignSize.Y;

	const bool bOrigAutoSize = SlotData.bAutoSize;
	SlotData = FLayoutCalculator::CalculateSlot(NormalizedBounds, Context);
	SlotData.bAutoSize = bOrigAutoSize;

	return SlotData;
}
