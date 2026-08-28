// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UCanvasPanel;
class UPanelWidget;
class UWidgetTree;

class FWidgetLayoutAdapter
{
public:
	/** Per-slot-group "differs from default FUMGSlotData" flags. */
	struct FNonDefaultGroups
	{
		/** Anchors, Alignment, or Offsets differ (canvas slot fields). */
		bool bCanvas = false;
		/** Padding differs from the default margin. */
		bool bPadding = false;
		/** Horizontal/Vertical alignment, SizeRule, or FillWeight differ (box slot fields). */
		bool bBox = false;
		/** Row/Column/RowSpan/ColumnSpan differ (grid slot fields). */
		bool bGrid = false;
	};

	/** Compare a slot against a default-constructed FUMGSlotData, grouped per slot family. */
	static FNonDefaultGroups GetNonDefaultSlotGroups(const FUMGSlotData& Slot);

	/** True when any slot field group differs from a default-constructed FUMGSlotData. */
	static bool HasMeaningfulSlot(const FUMGSlotData& Slot);

	/**
	 * Resolve the final slot for a widget being added to a parent panel.
	 * @param Node            Source widget node (provides Bounds and original Slot).
	 * @param ParentWidget    The panel widget this node is being added to.
	 * @param WidgetTree      The widget tree (used to check root widget).
	 * @param DesignSize      Current design canvas size.
	 * @param bEnableAdaptiveAnchors  Whether adaptive anchor calculation is allowed.
	 * @return Resolved slot data to apply to the panel slot.
	 */
	static FUMGSlotData Resolve(
		const FUMGWidgetNode& Node,
		UPanelWidget* ParentWidget,
		UWidgetTree* WidgetTree,
		const FIntPoint& DesignSize,
		bool bEnableAdaptiveAnchors);

private:
	static bool IsRootCanvasChild(UPanelWidget* ParentWidget, UWidgetTree* WidgetTree);
	static bool HasValidSlotHistory(const FUMGSlotData& Slot);
};
