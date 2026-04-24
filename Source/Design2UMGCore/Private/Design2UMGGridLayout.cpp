// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGGridLayout.h"

namespace
{
	bool HasExplicitGridSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& ((Node->LayoutData.HasLayoutRowSpanValue() && Node->LayoutData.LayoutRowSpan > 1)
				|| (Node->LayoutData.HasLayoutColumnSpanValue() && Node->LayoutData.LayoutColumnSpan > 1));
	}

	int32 ResolveGridRowSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() ? FMath::Max(1, Node->LayoutData.GetLayoutRowSpanOr(1)) : 1;
	}

	int32 ResolveGridColumnSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() ? FMath::Max(1, Node->LayoutData.GetLayoutColumnSpanOr(1)) : 1;
	}

	bool TryMapGridHorizontalAlignment(const FString& Value, FString& OutAlignment)
	{
		if (Value.Equals(TEXT("MIN"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LEFT"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Left");
			return true;
		}
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Center");
			return true;
		}
		if (Value.Equals(TEXT("MAX"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("RIGHT"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Right");
			return true;
		}
		if (Value.Equals(TEXT("STRETCH"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("FILL"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Fill");
			return true;
		}
		return false;
	}

	bool TryMapGridVerticalAlignment(const FString& Value, FString& OutAlignment)
	{
		if (Value.Equals(TEXT("MIN"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("TOP"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Top");
			return true;
		}
		if (Value.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Center");
			return true;
		}
		if (Value.Equals(TEXT("MAX"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("BOTTOM"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Bottom");
			return true;
		}
		if (Value.Equals(TEXT("STRETCH"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("FILL"), ESearchCase::IgnoreCase))
		{
			OutAlignment = TEXT("Fill");
			return true;
		}
		return false;
	}

	uint64 MakeGridCellKey(int32 Row, int32 Column)
	{
		return (static_cast<uint64>(static_cast<uint32>(Row)) << 32)
			| static_cast<uint32>(Column);
	}

	bool CanPlaceGridItem(
		const Design2UMGGridLayout::FGridPlacementState& State,
		int32 Row,
		int32 Column,
		int32 RowSpan,
		int32 ColumnSpan)
	{
		if (Row < 0 || Column < 0)
		{
			return false;
		}
		if (State.ColumnCount > 0 && (Column + ColumnSpan) > State.ColumnCount)
		{
			return false;
		}

		for (int32 RowOffset = 0; RowOffset < RowSpan; ++RowOffset)
		{
			for (int32 ColumnOffset = 0; ColumnOffset < ColumnSpan; ++ColumnOffset)
			{
				if (State.OccupiedCells.Contains(MakeGridCellKey(Row + RowOffset, Column + ColumnOffset)))
				{
					return false;
				}
			}
		}
		return true;
	}

	void MarkGridItemPlaced(
		Design2UMGGridLayout::FGridPlacementState& State,
		int32 Row,
		int32 Column,
		int32 RowSpan,
		int32 ColumnSpan)
	{
		for (int32 RowOffset = 0; RowOffset < RowSpan; ++RowOffset)
		{
			for (int32 ColumnOffset = 0; ColumnOffset < ColumnSpan; ++ColumnOffset)
			{
				State.OccupiedCells.Add(MakeGridCellKey(Row + RowOffset, Column + ColumnOffset));
			}
		}

		State.CursorRow = Row;
		State.CursorColumn = Column + ColumnSpan;
		while (State.CursorColumn >= State.ColumnCount)
		{
			State.CursorColumn -= State.ColumnCount;
			++State.CursorRow;
		}
	}

	bool TryFindGridPlacement(
		Design2UMGGridLayout::FGridPlacementState& State,
		int32 RowSpan,
		int32 ColumnSpan,
		const TOptional<int32>& FixedRow,
		const TOptional<int32>& FixedColumn,
		int32& OutRow,
		int32& OutColumn)
	{
		const int32 StartRow = FixedRow.IsSet() ? FixedRow.GetValue() : State.CursorRow;
		const int32 EndRow = FixedRow.IsSet()
			? FixedRow.GetValue()
			: FMath::Max(State.RowCount + 8, State.CursorRow + State.OccupiedCells.Num() + 8);

		for (int32 Row = StartRow; Row <= EndRow; ++Row)
		{
			const int32 StartColumn = FixedColumn.IsSet()
				? FixedColumn.GetValue()
				: ((Row == State.CursorRow) ? State.CursorColumn : 0);
			const int32 EndColumn = FixedColumn.IsSet()
				? FixedColumn.GetValue()
				: FMath::Max(0, State.ColumnCount - ColumnSpan);

			for (int32 Column = StartColumn; Column <= EndColumn; ++Column)
			{
				if (!CanPlaceGridItem(State, Row, Column, RowSpan, ColumnSpan))
				{
					continue;
				}

				OutRow = Row;
				OutColumn = Column;
				MarkGridItemPlaced(State, Row, Column, RowSpan, ColumnSpan);
				return true;
			}
		}

		return false;
	}
}

namespace Design2UMGGridLayout
{
	bool NeedsGridPanel(const TArray<TSharedPtr<FDesignNode>>& VisibleChildren)
	{
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (HasExplicitGridSpan(Child))
			{
				return true;
			}
		}
		return false;
	}

	float ResolveGridColumnGap(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}
		if (Node->LayoutData.HasLayoutColumnGapValue())
		{
			return FMath::Max(0.0f, Node->LayoutData.LayoutColumnGap);
		}
		return FMath::Max(0.0f, Node->LayoutData.Gap);
	}

	float ResolveGridRowGap(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}
		if (Node->LayoutData.HasLayoutRowGapValue())
		{
			return FMath::Max(0.0f, Node->LayoutData.LayoutRowGap);
		}
		return FMath::Max(0.0f, Node->LayoutData.Gap);
	}

	void ApplyGridChildAlignmentOverrides(
		FUMGWidgetNode& ChildWidget,
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& ChildNode)
	{
		if (!ParentNode.IsValid() || !ChildNode.IsValid())
		{
			return;
		}

		FString AlignmentValue;
		const FString HorizontalAlignSource = ChildNode->LayoutData.GetLayoutChildHorizontalAlignOr(
			ParentNode->LayoutData.GetLayoutChildHorizontalAlignOr(FString()));
		if (TryMapGridHorizontalAlignment(HorizontalAlignSource, AlignmentValue))
		{
			ChildWidget.Slot.HorizontalAlignment = AlignmentValue;
		}

		const FString VerticalAlignSource = ChildNode->LayoutData.GetLayoutChildVerticalAlignOr(
			ParentNode->LayoutData.GetLayoutChildVerticalAlignOr(FString()));
		if (TryMapGridVerticalAlignment(VerticalAlignSource, AlignmentValue))
		{
			ChildWidget.Slot.VerticalAlignment = AlignmentValue;
		}
	}

	int32 ResolveGridColumnCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren)
	{
		int32 ColumnCount = (Node.IsValid() && Node->LayoutData.HasLayoutColumnCountValue())
			? FMath::Max(1, Node->LayoutData.LayoutColumnCount)
			: 0;
		int32 ExplicitRequiredColumns = 0;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (!Child.IsValid())
			{
				continue;
			}

			const int32 AnchorColumn = FMath::Max(0, Child->LayoutData.GetLayoutColumnOr(0));
			ExplicitRequiredColumns = FMath::Max(
				ExplicitRequiredColumns,
				AnchorColumn + ResolveGridColumnSpan(Child));
		}

		ColumnCount = FMath::Max(ColumnCount, ExplicitRequiredColumns);
		if (ColumnCount <= 0 && Node.IsValid() && Node->LayoutData.HasLayoutRowCountValue() && Node->LayoutData.LayoutRowCount > 0)
		{
			ColumnCount = FMath::CeilToInt(static_cast<float>(VisibleChildren.Num()) / Node->LayoutData.LayoutRowCount);
		}

		return FMath::Max(1, ColumnCount > 0 ? ColumnCount : VisibleChildren.Num());
	}

	int32 ResolveGridRowCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		int32 ColumnCount)
	{
		int32 RowCount = (Node.IsValid() && Node->LayoutData.HasLayoutRowCountValue())
			? FMath::Max(1, Node->LayoutData.LayoutRowCount)
			: 0;
		int32 ExplicitRequiredRows = 0;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (!Child.IsValid())
			{
				continue;
			}

			const int32 AnchorRow = FMath::Max(0, Child->LayoutData.GetLayoutRowOr(0));
			ExplicitRequiredRows = FMath::Max(
				ExplicitRequiredRows,
				AnchorRow + ResolveGridRowSpan(Child));
		}

		RowCount = FMath::Max(RowCount, ExplicitRequiredRows);
		if (RowCount <= 0 && ColumnCount > 0)
		{
			RowCount = FMath::CeilToInt(static_cast<float>(VisibleChildren.Num()) / ColumnCount);
		}
		return FMath::Max(1, RowCount > 0 ? RowCount : VisibleChildren.Num());
	}

	void ApplyGridPanelChildSlotData(
		FUMGWidgetNode& ChildWidget,
		const TSharedPtr<FDesignNode>& ParentNode,
		const TSharedPtr<FDesignNode>& ChildNode,
		FGridPlacementState& State)
	{
		if (!ParentNode.IsValid() || !ChildNode.IsValid())
		{
			return;
		}

		const int32 RowSpan = ResolveGridRowSpan(ChildNode);
		const int32 ColumnSpan = ResolveGridColumnSpan(ChildNode);
		const TOptional<int32> FixedRow = (ChildNode->LayoutData.HasLayoutRowValue() && ChildNode->LayoutData.LayoutRow >= 0)
			? TOptional<int32>(ChildNode->LayoutData.LayoutRow)
			: TOptional<int32>();
		const TOptional<int32> FixedColumn = (ChildNode->LayoutData.HasLayoutColumnValue() && ChildNode->LayoutData.LayoutColumn >= 0)
			? TOptional<int32>(ChildNode->LayoutData.LayoutColumn)
			: TOptional<int32>();

		int32 Row = FixedRow.Get(FMath::Max(0, State.CursorRow));
		int32 Column = FixedColumn.Get(0);
		if (!TryFindGridPlacement(State, RowSpan, ColumnSpan, FixedRow, FixedColumn, Row, Column))
		{
			MarkGridItemPlaced(State, Row, Column, RowSpan, ColumnSpan);
		}

		ChildWidget.Slot.Row = Row;
		ChildWidget.Slot.Column = Column;
		ChildWidget.Slot.RowSpan = RowSpan;
		ChildWidget.Slot.ColumnSpan = ColumnSpan;
		ApplyGridChildAlignmentOverrides(ChildWidget, ParentNode, ChildNode);

		const float ColumnGap = ResolveGridColumnGap(ParentNode);
		const float RowGap = ResolveGridRowGap(ParentNode);
		if (ColumnGap <= KINDA_SMALL_NUMBER && RowGap <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float HalfColumnGap = ColumnGap * 0.5f;
		const float HalfRowGap = RowGap * 0.5f;
		const bool bTouchesLeftEdge = Column <= 0;
		const bool bTouchesTopEdge = Row <= 0;
		const bool bTouchesRightEdge = ParentNode->LayoutData.HasLayoutColumnCountValue()
			&& ParentNode->LayoutData.LayoutColumnCount > 0
			&& (Column + ColumnSpan) >= ParentNode->LayoutData.LayoutColumnCount;
		const bool bTouchesBottomEdge = ParentNode->LayoutData.HasLayoutRowCountValue()
			&& ParentNode->LayoutData.LayoutRowCount > 0
			&& (Row + RowSpan) >= ParentNode->LayoutData.LayoutRowCount;
		ChildWidget.Slot.Padding = FMargin(
			bTouchesLeftEdge ? 0.0f : HalfColumnGap,
			bTouchesTopEdge ? 0.0f : HalfRowGap,
			bTouchesRightEdge ? 0.0f : HalfColumnGap,
			bTouchesBottomEdge ? 0.0f : HalfRowGap);
	}
}
