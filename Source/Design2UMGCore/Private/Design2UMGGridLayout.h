// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

namespace Design2UMGGridLayout
{
	struct FGridPlacementState
	{
		int32 ColumnCount = 1;
		int32 RowCount = 0;
		int32 CursorRow = 0;
		int32 CursorColumn = 0;
		TSet<uint64> OccupiedCells;
	};

	bool NeedsGridPanel(const TArray<TSharedPtr<FDesignNode>>& VisibleChildren);
	float ResolveGridColumnGap(const TSharedPtr<FDesignNode>& Node);
	float ResolveGridRowGap(const TSharedPtr<FDesignNode>& Node);
	void ApplyGridChildAlignmentOverrides(
		FUMGWidgetNode& ChildWidget,
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& ChildNode);
	int32 ResolveGridColumnCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren);
	int32 ResolveGridRowCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		int32 ColumnCount);
	void ApplyGridPanelChildSlotData(
		FUMGWidgetNode& ChildWidget,
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& ChildNode,
		FGridPlacementState& State);
}
