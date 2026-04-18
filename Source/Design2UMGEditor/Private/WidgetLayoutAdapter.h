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
	static bool HasMeaningfulSlot(const FUMGSlotData& Slot);
	static bool HasValidSlotHistory(const FUMGSlotData& Slot);
};
