// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "PathWidgetBuilder.h"
#include "SizeEstimator.h"
#include "TextMeasurement.h"
#include "VisualMapper.h"
#include "RefExpander.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "Dom/JsonObject.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaxTreeRecursionDepth = 512;

	struct FConversionState
	{
		explicit FConversionState(const FDesignDocument& InDocument, TArray<FString>& InWarnings)
			: Document(InDocument)
			, Warnings(InWarnings)
		{
		}

		const FDesignDocument& Document;
		TArray<FString>& Warnings;
		TSet<FString> WarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TSet<FString> PrototypeTriggerNodeIds;
		TMap<FString, FString> PendingIconFontMappings;
		FUMGPrototypeNavigationData PrototypeNavigation;
		int32 RecursionDepth = 0;
	};

	struct FPrototypeNavigationAnalysis
	{
		FUMGPrototypeNavigationData Data;
		TSet<FString> TriggerNodeIds;
	};

	struct FResolvedPrototypeAction
	{
		EUMGPrototypeNavigationAction Kind = EUMGPrototypeNavigationAction::Navigate;
		FString Payload;
		FUMGPrototypeTransitionData Transition;
		bool bResolved = false;

		bool IsValid() const
		{
			return bResolved
				&& (Kind == EUMGPrototypeNavigationAction::Back || !Payload.IsEmpty());
		}
	};

	struct FConversionContext
	{
		explicit FConversionContext(FConversionState& InState)
			: State(InState)
		{
		}

		FConversionContext WithNodeTheme(const TSharedPtr<FDesignNode>& Node) const
		{
			FConversionContext Next(State);
			Next.ThemeContext = ThemeContext;
			if (Node.IsValid())
			{
				for (const auto& Pair : Node->Theme)
				{
					Next.ThemeContext.Add(Pair.Key, Pair.Value);
				}
			}
			return Next;
		}

		FConversionState& State;
		TMap<FString, FString> ThemeContext;
	};

	struct FRecursionGuard
	{
		explicit FRecursionGuard(int32& InDepth, const TCHAR* InName = TEXT(""))
			: Depth(InDepth)
			, bValid(InDepth < MaxTreeRecursionDepth)
		{
			if (bValid)
			{
				++Depth;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, MaxTreeRecursionDepth);
				UE_LOG(LogDesign2UMG, Error, TEXT("设计文件节点嵌套层级过深（超过 %d 层），可能存在循环引用。"), MaxTreeRecursionDepth);
			}
		}

		~FRecursionGuard()
		{
			if (bValid)
				--Depth;
		}

		int32& Depth;
		bool bValid = false;
	};

	const TCHAR* ToSizeModeLogString(EDesignSizeMode Mode)
	{
		switch (Mode)
		{
		case EDesignSizeMode::Fixed: return TEXT("fixed");
		case EDesignSizeMode::FillContainer: return TEXT("fill");
		case EDesignSizeMode::FitContent: return TEXT("fit");
		default: return TEXT("unknown");
		}
	}

	bool IsDesignerOnlyNodeType(EDesignNodeType Type)
	{
		return Type == EDesignNodeType::Connection
			|| Type == EDesignNodeType::Note
			|| Type == EDesignNodeType::Prompt
			|| Type == EDesignNodeType::Context;
	}

	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && (HasVisibleFill(Node->Fill) || Node->Fills.Num() > 0);
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
			return false;

		if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			return true;

		if (HasVisibleFill(Node->Fill))
			return true;

		for (const FDesignFill& Fill : Node->Fills)
		{
			if (HasVisibleFill(Fill))
				return true;
		}

		if (Node->Stroke.HasStroke())
			return true;

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled)
				return true;
		}

		return false;
	}

	struct FGridPlacementState
	{
		int32 ColumnCount = 1;
		int32 RowCount = 0;
		int32 CursorRow = 0;
		int32 CursorColumn = 0;
		TSet<uint64> OccupiedCells;
	};

	bool HasExplicitGridAnchor(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && (Node->bHasLayoutRow || Node->bHasLayoutColumn);
	}

	bool HasExplicitGridSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& ((Node->bHasLayoutRowSpan && Node->LayoutRowSpan > 1)
				|| (Node->bHasLayoutColumnSpan && Node->LayoutColumnSpan > 1));
	}

	int32 ResolveGridRowSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return (Node.IsValid() && Node->bHasLayoutRowSpan) ? FMath::Max(1, Node->LayoutRowSpan) : 1;
	}

	int32 ResolveGridColumnSpan(const TSharedPtr<FDesignNode>& Node)
	{
		return (Node.IsValid() && Node->bHasLayoutColumnSpan) ? FMath::Max(1, Node->LayoutColumnSpan) : 1;
	}

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
		if (Node->bHasLayoutColumnGap)
		{
			return FMath::Max(0.0f, Node->LayoutColumnGap);
		}
		return FMath::Max(0.0f, Node->Gap);
	}

	float ResolveGridRowGap(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}
		if (Node->bHasLayoutRowGap)
		{
			return FMath::Max(0.0f, Node->LayoutRowGap);
		}
		return FMath::Max(0.0f, Node->Gap);
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
		const FString HorizontalAlignSource = ChildNode->bHasLayoutChildHorizontalAlign
			? ChildNode->LayoutChildHorizontalAlign
			: ParentNode->LayoutChildHorizontalAlign;
		if (TryMapGridHorizontalAlignment(HorizontalAlignSource, AlignmentValue))
		{
			ChildWidget.Slot.HorizontalAlignment = AlignmentValue;
		}

		const FString VerticalAlignSource = ChildNode->bHasLayoutChildVerticalAlign
			? ChildNode->LayoutChildVerticalAlign
			: ParentNode->LayoutChildVerticalAlign;
		if (TryMapGridVerticalAlignment(VerticalAlignSource, AlignmentValue))
		{
			ChildWidget.Slot.VerticalAlignment = AlignmentValue;
		}
	}

	int32 ResolveGridColumnCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren)
	{
		int32 ColumnCount = (Node.IsValid() && Node->bHasLayoutColumnCount)
			? FMath::Max(1, Node->LayoutColumnCount)
			: 0;
		int32 ExplicitRequiredColumns = 0;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (!Child.IsValid())
			{
				continue;
			}

			const int32 AnchorColumn = (Child->bHasLayoutColumn && Child->LayoutColumn >= 0)
				? Child->LayoutColumn
				: 0;
			ExplicitRequiredColumns = FMath::Max(
				ExplicitRequiredColumns,
				AnchorColumn + ResolveGridColumnSpan(Child));
		}

		ColumnCount = FMath::Max(ColumnCount, ExplicitRequiredColumns);
		if (ColumnCount <= 0 && Node.IsValid() && Node->bHasLayoutRowCount && Node->LayoutRowCount > 0)
		{
			ColumnCount = FMath::CeilToInt(static_cast<float>(VisibleChildren.Num()) / Node->LayoutRowCount);
		}

		return FMath::Max(1, ColumnCount > 0 ? ColumnCount : VisibleChildren.Num());
	}

	int32 ResolveGridRowCount(
		const TSharedPtr<FDesignNode>& Node,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		int32 ColumnCount)
	{
		int32 RowCount = (Node.IsValid() && Node->bHasLayoutRowCount)
			? FMath::Max(1, Node->LayoutRowCount)
			: 0;
		int32 ExplicitRequiredRows = 0;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			if (!Child.IsValid())
			{
				continue;
			}

			const int32 AnchorRow = (Child->bHasLayoutRow && Child->LayoutRow >= 0)
				? Child->LayoutRow
				: 0;
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

	uint64 MakeGridCellKey(int32 Row, int32 Column)
	{
		return (static_cast<uint64>(static_cast<uint32>(Row)) << 32)
			| static_cast<uint32>(Column);
	}

	bool CanPlaceGridItem(
		const FGridPlacementState& State,
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
		FGridPlacementState& State,
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
		FGridPlacementState& State,
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
		const TOptional<int32> FixedRow = (ChildNode->bHasLayoutRow && ChildNode->LayoutRow >= 0)
			? TOptional<int32>(ChildNode->LayoutRow)
			: TOptional<int32>();
		const TOptional<int32> FixedColumn = (ChildNode->bHasLayoutColumn && ChildNode->LayoutColumn >= 0)
			? TOptional<int32>(ChildNode->LayoutColumn)
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
		const bool bTouchesRightEdge = ParentNode->bHasLayoutColumnCount
			&& ParentNode->LayoutColumnCount > 0
			&& (Column + ColumnSpan) >= ParentNode->LayoutColumnCount;
		const bool bTouchesBottomEdge = ParentNode->bHasLayoutRowCount
			&& ParentNode->LayoutRowCount > 0
			&& (Row + RowSpan) >= ParentNode->LayoutRowCount;
		ChildWidget.Slot.Padding = FMargin(
			bTouchesLeftEdge ? 0.0f : HalfColumnGap,
			bTouchesTopEdge ? 0.0f : HalfRowGap,
			bTouchesRightEdge ? 0.0f : HalfColumnGap,
			bTouchesBottomEdge ? 0.0f : HalfRowGap);
	}

	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
	{
		if (!Node.IsValid())
			return FString();
		if (Key == TEXT("sourceNodeType"))
			return Node->FigmaSourceNodeType;
		if (Key == TEXT("figma.renderFallbackImageRef"))
			return !Node->FigmaRenderFallback.ImageRef.IsEmpty()
				? Node->FigmaRenderFallback.ImageRef
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackScope"))
			return !Node->FigmaRenderFallback.Scope.IsEmpty()
				? Node->FigmaRenderFallback.Scope
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackSource"))
			return !Node->FigmaRenderFallback.Source.IsEmpty()
				? Node->FigmaRenderFallback.Source
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackReason"))
			return !Node->FigmaRenderFallback.Reason.IsEmpty()
				? Node->FigmaRenderFallback.Reason
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackOffsetX"))
			return !Node->FigmaRenderFallback.OffsetX.IsEmpty()
				? Node->FigmaRenderFallback.OffsetX
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackOffsetY"))
			return !Node->FigmaRenderFallback.OffsetY.IsEmpty()
				? Node->FigmaRenderFallback.OffsetY
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackWidth"))
			return !Node->FigmaRenderFallback.Width.IsEmpty()
				? Node->FigmaRenderFallback.Width
				: Node->Metadata.FindRef(Key);
		if (Key == TEXT("figma.renderFallbackHeight"))
			return !Node->FigmaRenderFallback.Height.IsEmpty()
				? Node->FigmaRenderFallback.Height
				: Node->Metadata.FindRef(Key);
		if (const FString* Value = Node->Metadata.Find(Key))
			return *Value;
		return FString();
	}

	FUMGFigmaNodeData BuildFigmaNodeData(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGFigmaNodeData Result;
		if (!Node.IsValid())
		{
			return Result;
		}

		Result.SourceNodeId = Node->Id;
		Result.SourceNodeName = Node->Name;
		Result.SourceNodeType = GetSourceExtension(Node, TEXT("sourceNodeType"));
		Result.ComponentId = Node->FigmaInstance.ComponentId;
		Result.ComponentSetId = Node->FigmaInstance.ComponentSetId;
		Result.MainComponentKey = Node->FigmaInstance.MainComponentKey;
		Result.ComponentPropertiesJson = Node->FigmaInstance.ComponentPropertiesJson;
		Result.VariantPropertiesJson = Node->FigmaInstance.VariantPropertiesJson;
		Result.OverridesJson = Node->FigmaInstance.OverridesJson;
		Result.bIsExposedInstance = Node->FigmaInstance.bIsExposedInstance;
		Result.bHasIsExposedInstance = Node->FigmaInstance.bHasIsExposedInstance;

		if (Result.IsComponentDefinition() && Result.ComponentId.IsEmpty())
		{
			Result.ComponentId = Node->Id;
		}
		if (Result.IsComponentSet() && Result.ComponentSetId.IsEmpty())
		{
			Result.ComponentSetId = Node->Id;
		}

		return Result;
	}

	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType)
	{
		return GetSourceExtension(Node, TEXT("sourceNodeType")).Equals(ExpectedType, ESearchCase::IgnoreCase);
	}

	bool TryNormalizeValidFontAssetPath(const FString& InPath, FString& OutPath)
	{
		OutPath = InPath.TrimStartAndEnd();
		if (OutPath.IsEmpty())
		{
			return false;
		}

		if (LoadObject<UFont>(nullptr, *OutPath))
		{
			return true;
		}

		if (!OutPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(OutPath);
			if (!AssetName.IsEmpty())
			{
				const FString CandidateObjectPath = OutPath + TEXT(".") + AssetName;
				if (LoadObject<UFont>(nullptr, *CandidateObjectPath))
				{
					OutPath = CandidateObjectPath;
					return true;
				}
			}
		}

		return false;
	}

	struct FChildSizeSnapshot
	{
		TSharedPtr<FDesignNode> Child;
		FDesignSize RawWidth;
		FDesignSize RawHeight;
		FDesignSize EffectiveWidth;
		FDesignSize EffectiveHeight;
		bool bUsedExpandedRef = false;
	};

	TSharedPtr<FDesignNode> ExpandRefForConversion(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ref)
		{
			return Node;
		}

		TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Context.State.Document);
		if (!Expanded.IsValid())
		{
			return nullptr;
		}

		if (Context.ThemeContext.Num() > 0)
		{
			FDesignVariableResolver Resolver(Context.State.Document.Variables);
			Resolver.SetThemeContext(Context.ThemeContext);
			FRefExpander::ReThemeNode(Expanded, Resolver);
		}

		return Expanded;
	}

	FChildSizeSnapshot BuildChildSizeSnapshot(const TSharedPtr<FDesignNode>& Child, const FDesignDocument& Doc)
	{
		FChildSizeSnapshot Snapshot;
		Snapshot.Child = Child;
		if (!Child.IsValid())
			return Snapshot;

		Snapshot.RawWidth = Child->Width;
		Snapshot.RawHeight = Child->Height;
		Snapshot.EffectiveWidth = Child->Width;
		Snapshot.EffectiveHeight = Child->Height;

		if (Child->Type == EDesignNodeType::Ref)
		{
			if (auto Expanded = FRefExpander::ExpandRef(Child, Doc))
			{
				Snapshot.bUsedExpandedRef = true;
				if (!Child->bHasWidth)
					Snapshot.EffectiveWidth = Expanded->Width;
				if (!Child->bHasHeight)
					Snapshot.EffectiveHeight = Expanded->Height;
			}
		}
		return Snapshot;
	}

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->bEnabled || IsDesignerOnlyNodeType(Node->Type))
			return true;

		// Ref 节点必须保留，后续展开后才有实际内容
		if (Node->Type == EDesignNodeType::Ref)
			return false;

		// Keep explicit flow containers (including empty GRID/WRAP/H/V frames) so layout semantics are not dropped.
		if (Node->Children.Num() == 0 && !HasRenderableVisual(Node))
		{
			const bool bIsExplicitFlowContainer =
				(Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
				&& Node->Layout != EDesignLayout::None;
			if (!bIsExplicitFlowContainer)
				return true;
		}

		return false;
	}

	bool IsHexColorLiteral(const FString& ColorValue)
	{
		if (!ColorValue.StartsWith(TEXT("#")))
			return false;
		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	}

	FString BuildHexColorWithAlpha(const FString& ColorValue, uint8 Alpha)
	{
		if (!IsHexColorLiteral(ColorValue))
			return FString();

		FColor Parsed = FColor::FromHex(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!IsHexColorLiteral(ColorValue))
			return true;

		const FColor Parsed = FColor::FromHex(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= 0x20) || ((MaxChannel - MinChannel) <= 0x10);
	}

	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		const FString* Value = Node->Metadata.Find(Key);
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	bool HasMeaningfulNodeMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		const FString Value = GetSourceExtension(Node, Key).TrimStartAndEnd();
		return !Value.IsEmpty()
			&& !Value.Equals(TEXT("[]"), ESearchCase::CaseSensitive)
			&& !Value.Equals(TEXT("{}"), ESearchCase::CaseSensitive)
			&& !Value.Equals(TEXT("null"), ESearchCase::IgnoreCase);
	}

	bool ShouldUseFigmaRenderFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString Scope = GetSourceExtension(Node, TEXT("figma.renderFallbackScope"));
		const bool bReplaceSubtree = Scope.Equals(TEXT("subtree"), ESearchCase::IgnoreCase);
		if (Node->Children.Num() != 0 && !bReplaceSubtree)
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text
			|| Node->Type == EDesignNodeType::IconFont
			|| Node->Type == EDesignNodeType::Ref)
		{
			return false;
		}

		const FString TexturePath = GetSourceExtension(Node, TEXT("figma.renderFallbackImageRef"));
		if (TexturePath.IsEmpty())
		{
			return false;
		}

		if (!Scope.IsEmpty()
			&& !Scope.Equals(TEXT("full"), ESearchCase::IgnoreCase)
			&& !bReplaceSubtree)
		{
			return false;
		}

		const FString SourceNodeType = GetSourceExtension(Node, TEXT("sourceNodeType"));
		if (SourceNodeType.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (Node->FigmaPrototype.Interactions.Num() > 0 || HasMeaningfulNodeMetadata(Node, TEXT("interactions")))
		{
			return false;
		}

		return true;
	}

	FUMGWidgetNode CreateFigmaRenderFallbackWidget(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Widget;
		Widget.Type = TEXT("Image");

		const FString TexturePath = GetSourceExtension(Node, TEXT("figma.renderFallbackImageRef"));
		Widget.Brush.TexturePath = TexturePath;
		Widget.Brush.Image.TexturePath = TexturePath;
		Widget.Brush.Image.bHasTexturePath = true;

		float WidthOverride = Node->Width.Value;
		const FString FallbackWidth = GetSourceExtension(Node, TEXT("figma.renderFallbackWidth"));
		if (!FallbackWidth.IsEmpty())
		{
			WidthOverride = FCString::Atof(*FallbackWidth);
		}

		float HeightOverride = Node->Height.Value;
		const FString FallbackHeight = GetSourceExtension(Node, TEXT("figma.renderFallbackHeight"));
		if (!FallbackHeight.IsEmpty())
		{
			HeightOverride = FCString::Atof(*FallbackHeight);
		}

		if (WidthOverride > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = WidthOverride;
			Widget.WidgetStyle.bHasWidthOverride = true;
		}
		if (HeightOverride > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = HeightOverride;
			Widget.WidgetStyle.bHasHeightOverride = true;
		}

		return Widget;
	}

	bool HasWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetString(Widget, Key);
	}

	bool ShouldLockExplicitFitDimensionForFlow(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Children.Num() == 0
			&& (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			&& !Node->Content.IsEmpty();
	}

	bool TryApplyPlaceholderSurfaceTint(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		const UDesign2UMGSettings::FResolvedPlaceholderHatchStyle Style =
			Settings ? Settings->ResolvePlaceholderHatchStyle() : UDesign2UMGSettings::FResolvedPlaceholderHatchStyle();
		const bool bExpandedRefInstancePlaceholder = HasNodeMetadataFlag(Node, TEXT("design2umg.expanded_ref_instance"));
		const FString& DefaultPlaceholderFillColor =
			bExpandedRefInstancePlaceholder ? Style.RefFillColor : Style.BaseFillColor;
		const FString& DefaultPlaceholderOutlineColor =
			bExpandedRefInstancePlaceholder ? Style.RefStripeColor : Style.BaseStripeColor;
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		const bool bIsExplicitEmptySlotSurface =
			HasNodeMetadataFlag(Node, TEXT("pen.slot.empty")) || HasNodeMetadataFlag(Node, TEXT("figma.slot.empty"));
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Width.Value >= 24.0f
			&& Node->Height.Value >= 12.0f
			&& (Node->bSemanticPlaceholderSurface || bIsExplicitEmptySlotSurface);
		if (!bUseSlotPlaceholderTint)
			return false;

		const FString FillColor = IsHexColorLiteral(Node->Fill.SolidColor) ? Node->Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(Node->Stroke.Fill.SolidColor) ? Node->Stroke.Fill.SolidColor : FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, 0x33);
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
			return false;

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = TEXT("Image");
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Width.Value * 0.25f, Node->Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		for (const TCHAR* Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("DrawAs"), TEXT("OutlineThickness"), TEXT("OutlineColor"), TEXT("StrokePadding"), TEXT("StrokeMargin")})
			Widget.Properties.Remove(Key);
		Widget.Brush.MaterialRef = TEXT("placeholdersurface");
		Widget.Brush.bHasMaterialRef = true;
		Widget.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":%d,\"StripeGap\":%d,\"Inset\":%d}"),
			*PlaceholderFillColor,
			*PlaceholderOutlineColor,
			Style.StripeWidthPx,
			Style.StripeGapPx,
			InsetPx);
		Widget.Brush.bHasMaterialParams = true;
		if (Node->Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Width.Value;
			Widget.WidgetStyle.bHasWidthOverride = true;
		}
		if (Node->Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Height.Value;
			Widget.WidgetStyle.bHasHeightOverride = true;
		}
		return true;
	}

	bool ShouldWrapNodeWithBorder(const TSharedPtr<FDesignNode>& Node, bool bPlaceholderUsesDirectImage)
	{
		const bool bNeedsPaddingWrapper =
			FLayoutMapper::HasPadding(Node)
			&& (Node->bHasPaddingRuntimeWrapDecision
				? Node->bPaddingRequiresRuntimeWrap
				: ((Node->Layout == EDesignLayout::None)
					|| ((Node->Layout == EDesignLayout::Wrap || Node->Layout == EDesignLayout::Grid)
						&& Node->Children.Num() > 0)));

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& (HasVisualFill(Node)
				|| bNeedsPaddingWrapper
				|| Node->CornerRadius.HasRadius()
				|| Node->Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	bool ShouldKeepStructuralLeafInFlow(const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
	{
		if (!Node.IsValid()
			|| (ParentLayout != EDesignLayout::Wrap && ParentLayout != EDesignLayout::Grid))
		{
			return false;
		}

		if (Node->Children.Num() > 0 || HasRenderableVisual(Node))
		{
			return false;
		}

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& Node->Width.Value > 0.0f
			&& Node->Height.Value > 0.0f;
	}

	void GatherVisibleChildrenForConversion(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child) || ShouldKeepStructuralLeafInFlow(Child, Node->Layout))
			{
				OutVisibleChildren.Add(Child);
			}
		}
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->HasMultiFill())
			Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node);

		const FDesignShadow* InnerShadow = nullptr;
		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (!Effect.bEnabled || Effect.Type != EDesignEffectType::Shadow || !Effect.Shadow.bInner)
			{
				continue;
			}

			if (!InnerShadow || Effect.Shadow.Blur > InnerShadow->Blur)
			{
				InnerShadow = &Effect.Shadow;
			}
		}

		if (InnerShadow && Node.IsValid() && Node->Width.Value > 0.0f && Node->Height.Value > 0.0f)
		{
			Widget = FVisualMapper::WrapWithInnerShadow(MoveTemp(Widget), Node, *InnerShadow);
		}

		int32 ShadowCount = 0;
		ShadowCount = Widget.Shadow.BoxShadows.Num();
		if (ShadowCount > 0)
		{
			const TArray<FUMGBoxShadowLayerData> BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			for (int32 Index = 0; Index < ShadowCount; ++Index)
			{
				Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, BoxShadows[Index], Index);
			}
		}

		float MaxForegroundBlur = 0.0f;
		float MaxBackgroundBlur = 0.0f;
		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (!Effect.bEnabled || Effect.Radius <= 0.0f)
			{
				continue;
			}

			if (Effect.Type == EDesignEffectType::Blur)
			{
				MaxForegroundBlur = FMath::Max(MaxForegroundBlur, Effect.Radius);
			}
			else if (Effect.Type == EDesignEffectType::BackgroundBlur)
			{
				MaxBackgroundBlur = FMath::Max(MaxBackgroundBlur, Effect.Radius);
			}
		}

		if (MaxForegroundBlur > 0.0f && Node.IsValid() && Node->Width.Value > 0.0f && Node->Height.Value > 0.0f)
		{
			Widget = FVisualMapper::WrapWithForegroundBlur(MoveTemp(Widget), Node, MaxForegroundBlur);
		}

		if (MaxBackgroundBlur > 0.0f)
		{
			Widget = FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), MaxBackgroundBlur);
		}

		return Widget;
	}

	FString ResolvePrototypeActionDestinationId(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFigmaPrototypeAction& Action)
	{
		if (!Action.DestinationId.IsEmpty())
		{
			return Action.DestinationId;
		}
		if (!Action.TransitionNodeId.IsEmpty())
		{
			return Action.TransitionNodeId;
		}
		return Node.IsValid() ? Node->FigmaPrototype.TransitionNodeId : FString();
	}

	bool IsPrototypeNavigateNavigation(const FString& Value)
	{
		return Value.IsEmpty()
			|| Value.Equals(TEXT("NAVIGATE"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("SWAP"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeOverlayKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("OVERLAY"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_OVERLAY"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("SWAP_OVERLAY"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeBackKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("BACK"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeOpenLinkKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("URL"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_URL"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_LINK"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LINK"), ESearchCase::IgnoreCase);
	}

	bool LooksLikeExternalPrototypeLink(const FString& Value)
	{
		return Value.Contains(TEXT("://"))
			|| Value.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase)
			|| Value.StartsWith(TEXT("tel:"), ESearchCase::IgnoreCase)
			|| Value.StartsWith(TEXT("www."), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeNavigateAction(const FDesignFigmaPrototypeAction& Action)
	{
		return Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
			&& IsPrototypeNavigateNavigation(Action.Navigation);
	}

	bool IsPrototypeOverlayAction(const FDesignFigmaPrototypeAction& Action)
	{
		const bool bOverlayKeyword =
			IsPrototypeOverlayKeyword(Action.Navigation) || IsPrototypeOverlayKeyword(Action.Type);
		const bool bNodeLike =
			Action.Type.IsEmpty()
			|| Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
			|| IsPrototypeOverlayKeyword(Action.Type);
		return bOverlayKeyword && bNodeLike;
	}

	bool IsPrototypeBackAction(const FDesignFigmaPrototypeAction& Action)
	{
		return IsPrototypeBackKeyword(Action.Type) || IsPrototypeBackKeyword(Action.Navigation);
	}

	bool IsPrototypeOpenLinkAction(
		const FDesignFigmaPrototypeAction& Action,
		const FString& DestinationId)
	{
		return IsPrototypeOpenLinkKeyword(Action.Type)
			|| IsPrototypeOpenLinkKeyword(Action.Navigation)
			|| LooksLikeExternalPrototypeLink(DestinationId);
	}

	const TCHAR* DescribePrototypeActionKind(const EUMGPrototypeNavigationAction Kind)
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationAction::Navigate:
			return TEXT("navigation");
		case EUMGPrototypeNavigationAction::Overlay:
			return TEXT("overlay");
		case EUMGPrototypeNavigationAction::Back:
			return TEXT("back");
		case EUMGPrototypeNavigationAction::OpenLink:
			return TEXT("open link");
		default:
			return TEXT("prototype action");
		}
	}

	FDesignFigmaPrototypeTransitionData ResolvePrototypeTransitionSource(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFigmaPrototypeAction& Action)
	{
		FDesignFigmaPrototypeTransitionData Resolved;
		if (Node.IsValid() && Node->FigmaPrototype.Transition.HasAny())
		{
			Resolved = Node->FigmaPrototype.Transition;
		}

		if (!Action.Transition.HasAny())
		{
			return Resolved;
		}

		if (!Action.Transition.Type.IsEmpty())
		{
			Resolved.Type = Action.Transition.Type;
		}
		if (!Action.Transition.Direction.IsEmpty())
		{
			Resolved.Direction = Action.Transition.Direction;
		}
		if (Action.Transition.bHasDuration)
		{
			Resolved.Duration = Action.Transition.Duration;
			Resolved.bHasDuration = true;
		}
		if (!Action.Transition.EasingType.IsEmpty())
		{
			Resolved.EasingType = Action.Transition.EasingType;
		}
		if (!Action.Transition.EasingJson.IsEmpty())
		{
			Resolved.EasingJson = Action.Transition.EasingJson;
		}
		if (Action.Transition.bHasMatchLayers)
		{
			Resolved.bMatchLayers = Action.Transition.bMatchLayers;
			Resolved.bHasMatchLayers = true;
		}

		return Resolved;
	}

	FUMGPrototypeTransitionData BuildPrototypeTransition(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFigmaPrototypeAction& Action)
	{
		const FDesignFigmaPrototypeTransitionData Source = ResolvePrototypeTransitionSource(Node, Action);

		FUMGPrototypeTransitionData Transition;
		Transition.Type = Source.Type.ToUpper();
		Transition.Direction = Source.Direction.ToUpper();
		Transition.EasingType = Source.EasingType.ToUpper();
		Transition.bMatchLayers = Source.bMatchLayers;
		Transition.bHasMatchLayers = Source.bHasMatchLayers;

		if (Source.bHasDuration)
		{
			Transition.DurationSeconds = FMath::Max(0.0f, Source.Duration / 1000.0f);
		}
		else if (!Transition.Type.IsEmpty())
		{
			Transition.DurationSeconds = 0.2f;
		}

		if (Transition.Type.IsEmpty() && Transition.DurationSeconds > KINDA_SMALL_NUMBER)
		{
			Transition.Type = TEXT("DISSOLVE");
		}

		return Transition;
	}

	FString SerializePrototypeJsonValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		if (FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
		{
			return Json;
		}
		return FString();
	}

	FString SerializePrototypeJsonObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		if (FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
		{
			return Json;
		}
		return FString();
	}

	bool TryParsePrototypeJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString GetPrototypeJsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		FString Value;
		if (Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return FString();
	}

	bool TryGetPrototypeJsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, double& OutValue)
	{
		return Object.IsValid() && Object->TryGetNumberField(FieldName, OutValue);
	}

	TSharedPtr<FJsonObject> GetPrototypeJsonObject(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (Object->TryGetObjectField(FieldName, ObjectPtr) && ObjectPtr != nullptr)
		{
			return *ObjectPtr;
		}
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetPrototypeJsonArray(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
		if (Object->TryGetArrayField(FieldName, ArrayPtr) && ArrayPtr != nullptr)
		{
			return ArrayPtr;
		}
		return nullptr;
	}

	void ParsePrototypeTransitionFromJsonObject(
		const TSharedPtr<FJsonObject>& TransitionObject,
		FDesignFigmaPrototypeTransitionData& OutTransition)
	{
		if (!TransitionObject.IsValid())
		{
			return;
		}

		OutTransition.Type = GetPrototypeJsonString(TransitionObject, TEXT("type"));
		OutTransition.Direction = GetPrototypeJsonString(TransitionObject, TEXT("direction"));
		double DurationValue = 0.0;
		if (TryGetPrototypeJsonNumber(TransitionObject, TEXT("duration"), DurationValue))
		{
			OutTransition.Duration = static_cast<float>(DurationValue);
			OutTransition.bHasDuration = true;
		}

		if (const TSharedPtr<FJsonObject> EasingObject = GetPrototypeJsonObject(TransitionObject, TEXT("easing")))
		{
			OutTransition.EasingJson = SerializePrototypeJsonObject(EasingObject);
			OutTransition.EasingType = GetPrototypeJsonString(EasingObject, TEXT("type"));
		}

		bool bMatchLayers = false;
		if (TransitionObject->TryGetBoolField(TEXT("matchLayers"), bMatchLayers))
		{
			OutTransition.bMatchLayers = bMatchLayers;
			OutTransition.bHasMatchLayers = true;
		}
	}

	FDesignFigmaPrototypeAction ParsePrototypeActionFromJsonObject(const TSharedPtr<FJsonObject>& ActionObject)
	{
		FDesignFigmaPrototypeAction Action;
		if (!ActionObject.IsValid())
		{
			return Action;
		}

		Action.RawJson = SerializePrototypeJsonObject(ActionObject);
		Action.Type = GetPrototypeJsonString(ActionObject, TEXT("type"));
		Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("destinationId"));
		if (Action.DestinationId.IsEmpty())
		{
			Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("url"));
		}
		if (Action.DestinationId.IsEmpty())
		{
			Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("uri"));
		}
		Action.Navigation = GetPrototypeJsonString(ActionObject, TEXT("navigation"));
		Action.TransitionNodeId = GetPrototypeJsonString(ActionObject, TEXT("transitionNodeID"));
		if (const TSharedPtr<FJsonObject> TransitionObject = GetPrototypeJsonObject(ActionObject, TEXT("transition")))
		{
			Action.TransitionJson = SerializePrototypeJsonObject(TransitionObject);
			ParsePrototypeTransitionFromJsonObject(TransitionObject, Action.Transition);
		}
		return Action;
	}

	EUMGPrototypeNavigationAction MapPrototypeStepKindToLegacyAction(const EUMGPrototypeNavigationStepKind Kind)
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
			return EUMGPrototypeNavigationAction::Navigate;
		case EUMGPrototypeNavigationStepKind::Overlay:
			return EUMGPrototypeNavigationAction::Overlay;
		case EUMGPrototypeNavigationStepKind::Back:
			return EUMGPrototypeNavigationAction::Back;
		case EUMGPrototypeNavigationStepKind::OpenLink:
			return EUMGPrototypeNavigationAction::OpenLink;
		case EUMGPrototypeNavigationStepKind::SetVariable:
			return EUMGPrototypeNavigationAction::SetVariable;
		case EUMGPrototypeNavigationStepKind::SetVariableMode:
			return EUMGPrototypeNavigationAction::SetVariableMode;
		case EUMGPrototypeNavigationStepKind::ConditionJump:
		case EUMGPrototypeNavigationStepKind::Jump:
			return EUMGPrototypeNavigationAction::Conditional;
		default:
			return EUMGPrototypeNavigationAction::Navigate;
		}
	}

	bool ResolvePrototypePageTarget(
		const TSharedPtr<FDesignNode>& Node,
		const EUMGPrototypeNavigationAction ActionKind,
		const FString& TargetNodeId,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		FUMGPrototypeNavigationStep& InOutStep,
		TArray<FString>& Warnings)
	{
		const FString* ResolvedPageId = NodeToPageId.Find(TargetNodeId);
		if (!ResolvedPageId)
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Prototype %s skipped on '%s': unresolved destination '%s'"),
				DescribePrototypeActionKind(ActionKind),
				Node.IsValid() ? *Node->Id : TEXT("<unknown>"),
				*TargetNodeId));
			return false;
		}

		const int32* ResolvedPageIndex = PageIdToIndex.Find(*ResolvedPageId);
		if (!ResolvedPageIndex || *ResolvedPageIndex < 0)
		{
			return false;
		}

		InOutStep.TargetPageId = *ResolvedPageId;
		InOutStep.TargetPageIndex = *ResolvedPageIndex;
		return true;
	}

	bool AppendPrototypeActionStepFromTypedAction(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFigmaPrototypeAction& Action,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationStep>& OutSteps,
		TArray<FString>& Warnings)
	{
		const FString DirectDestinationId = Action.DestinationId;
		const FString ResolvedNodeDestinationId = ResolvePrototypeActionDestinationId(Node, Action);
		FUMGPrototypeNavigationStep Step;
		Step.Transition = BuildPrototypeTransition(Node, Action);

		if (IsPrototypeBackAction(Action))
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Back;
		}
		else if (IsPrototypeOpenLinkAction(Action, DirectDestinationId) && !DirectDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::OpenLink;
			Step.ExternalUrl = DirectDestinationId;
		}
		else if (IsPrototypeOverlayAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Overlay;
			if (!ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Overlay,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings))
			{
				return false;
			}
		}
		else if (IsPrototypeNavigateAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Navigate;
			if (!ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Navigate,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings))
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		if (!Step.IsValid())
		{
			return false;
		}

		OutSteps.Add(MoveTemp(Step));
		return true;
	}

	bool AppendPrototypeActionStepsFromJsonObject(
		const TSharedPtr<FDesignNode>& Node,
		const TSharedPtr<FJsonObject>& ActionObject,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationStep>& OutSteps,
		TArray<FString>& Warnings)
	{
		if (!ActionObject.IsValid())
		{
			return false;
		}

		const FDesignFigmaPrototypeAction ParsedAction = ParsePrototypeActionFromJsonObject(ActionObject);
		const FString ActionTypeUpper = ParsedAction.Type.ToUpper();

		if (ActionTypeUpper.Equals(TEXT("SET_VARIABLE"), ESearchCase::CaseSensitive))
		{
			FUMGPrototypeNavigationStep Step;
			Step.Kind = EUMGPrototypeNavigationStepKind::SetVariable;
			Step.VariableId = GetPrototypeJsonString(ActionObject, TEXT("variableId"));
			const TSharedPtr<FJsonValue> VariableValueField = ActionObject->TryGetField(TEXT("variableValue"));
			const TSharedPtr<FJsonValue> LegacyValueField = ActionObject->TryGetField(TEXT("value"));
			Step.VariableValueJson = SerializePrototypeJsonValue(
				VariableValueField.IsValid() ? VariableValueField : LegacyValueField);
			if (!Step.IsValid())
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype set variable skipped on '%s': missing variable payload"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}
			OutSteps.Add(MoveTemp(Step));
			return true;
		}

		if (ActionTypeUpper.Equals(TEXT("SET_VARIABLE_MODE"), ESearchCase::CaseSensitive))
		{
			FUMGPrototypeNavigationStep Step;
			Step.Kind = EUMGPrototypeNavigationStepKind::SetVariableMode;
			Step.VariableCollectionId = GetPrototypeJsonString(ActionObject, TEXT("variableCollectionId"));
			Step.VariableModeId = GetPrototypeJsonString(ActionObject, TEXT("variableModeId"));
			if (!Step.IsValid())
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype set variable mode skipped on '%s': missing mode payload"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}
			OutSteps.Add(MoveTemp(Step));
			return true;
		}

		const bool bLooksConditional =
			ActionTypeUpper.Equals(TEXT("CONDITIONAL"), ESearchCase::CaseSensitive)
			|| GetPrototypeJsonArray(ActionObject, TEXT("conditionalBlocks")) != nullptr;
		if (bLooksConditional)
		{
			const TArray<TSharedPtr<FJsonValue>>* ConditionalBlocks = GetPrototypeJsonArray(ActionObject, TEXT("conditionalBlocks"));
			if (!ConditionalBlocks || ConditionalBlocks->Num() == 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype conditional skipped on '%s': no conditional blocks"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}

			const int32 StartIndex = OutSteps.Num();
			int32 PendingFalseJumpIndex = INDEX_NONE;
			TArray<int32> ExitJumpIndices;
			for (const TSharedPtr<FJsonValue>& BlockValue : *ConditionalBlocks)
			{
				const TSharedPtr<FJsonObject> BlockObject = BlockValue->AsObject();
				if (!BlockObject.IsValid())
				{
					continue;
				}

				const int32 BlockStartIndex = OutSteps.Num();
				if (PendingFalseJumpIndex != INDEX_NONE)
				{
					OutSteps[PendingFalseJumpIndex].JumpTargetIndex = BlockStartIndex;
				}

				FUMGPrototypeNavigationStep ConditionStep;
				ConditionStep.Kind = EUMGPrototypeNavigationStepKind::ConditionJump;
				ConditionStep.ConditionJson = SerializePrototypeJsonValue(BlockObject->TryGetField(TEXT("condition")));
				const int32 ConditionStepIndex = OutSteps.Add(MoveTemp(ConditionStep));
				PendingFalseJumpIndex = ConditionStepIndex;

				if (const TArray<TSharedPtr<FJsonValue>>* NestedActions = GetPrototypeJsonArray(BlockObject, TEXT("actions")))
				{
					for (const TSharedPtr<FJsonValue>& NestedActionValue : *NestedActions)
					{
						const TSharedPtr<FJsonObject> NestedActionObject = NestedActionValue->AsObject();
						if (!NestedActionObject.IsValid())
						{
							continue;
						}

						AppendPrototypeActionStepsFromJsonObject(
							Node,
							NestedActionObject,
							NodeToPageId,
							PageIdToIndex,
							OutSteps,
							Warnings);
					}
				}

				FUMGPrototypeNavigationStep ExitJumpStep;
				ExitJumpStep.Kind = EUMGPrototypeNavigationStepKind::Jump;
				const int32 ExitJumpIndex = OutSteps.Add(MoveTemp(ExitJumpStep));
				ExitJumpIndices.Add(ExitJumpIndex);
			}

			if (OutSteps.Num() == StartIndex)
			{
				return false;
			}

			const int32 AfterConditionalIndex = OutSteps.Num();
			if (PendingFalseJumpIndex != INDEX_NONE)
			{
				OutSteps[PendingFalseJumpIndex].JumpTargetIndex = AfterConditionalIndex;
			}
			for (const int32 ExitJumpIndex : ExitJumpIndices)
			{
				OutSteps[ExitJumpIndex].JumpTargetIndex = AfterConditionalIndex;
			}

			return true;
		}

		if (AppendPrototypeActionStepFromTypedAction(
			Node,
			ParsedAction,
			NodeToPageId,
			PageIdToIndex,
			OutSteps,
			Warnings))
		{
			return true;
		}

		if (!ParsedAction.Type.IsEmpty())
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Prototype action '%s' on node '%s' not supported yet"),
				*ParsedAction.Type,
				Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
		}
		return false;
	}

	FString NormalizePrototypeTriggerType(const FString& TriggerType)
	{
		if (FUMGPrototypeNavigationBinding::IsClickTriggerType(TriggerType))
		{
			return TEXT("ON_CLICK");
		}
		if (FUMGPrototypeNavigationBinding::IsHoverTriggerType(TriggerType))
		{
			return TEXT("ON_HOVER");
		}
		if (FUMGPrototypeNavigationBinding::IsPressTriggerType(TriggerType))
		{
			return TEXT("ON_PRESS");
		}
		if (FUMGPrototypeNavigationBinding::IsDragTriggerType(TriggerType))
		{
			return TEXT("ON_DRAG");
		}
		if (FUMGPrototypeNavigationBinding::IsKeyDownTriggerType(TriggerType))
		{
			return TEXT("ON_KEY_DOWN");
		}
		if (FUMGPrototypeNavigationBinding::IsKeyUpTriggerType(TriggerType))
		{
			return TEXT("ON_KEY_UP");
		}

		return TriggerType;
	}

	TArray<FString> ParsePrototypeTriggerKeyCodes(const FString& TriggerJson)
	{
		TArray<FString> KeyCodes;
		TSharedPtr<FJsonObject> TriggerObject;
		if (!TryParsePrototypeJsonObject(TriggerJson, TriggerObject))
		{
			return KeyCodes;
		}

		if (const TArray<TSharedPtr<FJsonValue>>* KeyCodeArray = GetPrototypeJsonArray(TriggerObject, TEXT("keyCodes")))
		{
			for (const TSharedPtr<FJsonValue>& KeyCodeValue : *KeyCodeArray)
			{
				FString KeyCodeString;
				if (KeyCodeValue.IsValid() && KeyCodeValue->TryGetString(KeyCodeString) && !KeyCodeString.IsEmpty())
				{
					KeyCodes.AddUnique(KeyCodeString.ToUpper());
				}
			}
		}

		const FString SingleKeyCode = GetPrototypeJsonString(TriggerObject, TEXT("keyCode"));
		if (!SingleKeyCode.IsEmpty())
		{
			KeyCodes.AddUnique(SingleKeyCode.ToUpper());
		}

		return KeyCodes;
	}

	bool BuildPrototypeBindingSummary(
		const TArray<FUMGPrototypeNavigationStep>& Steps,
		FResolvedPrototypeAction& OutSummary)
	{
		if (Steps.Num() == 0)
		{
			return false;
		}

		OutSummary = FResolvedPrototypeAction();
		for (const FUMGPrototypeNavigationStep& Step : Steps)
		{
			switch (Step.Kind)
			{
			case EUMGPrototypeNavigationStepKind::Navigate:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Navigate;
				OutSummary.Payload = Step.TargetPageId;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::Overlay:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Overlay;
				OutSummary.Payload = Step.TargetPageId;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::Back:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Back;
				OutSummary.Payload.Reset();
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::OpenLink:
				OutSummary.Kind = EUMGPrototypeNavigationAction::OpenLink;
				OutSummary.Payload = Step.ExternalUrl;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			default:
				break;
			}
		}

		const FUMGPrototypeNavigationStep& FirstStep = Steps[0];
		OutSummary.Kind = MapPrototypeStepKindToLegacyAction(FirstStep.Kind);
		OutSummary.Transition = FirstStep.Transition;
		OutSummary.bResolved = true;
		return true;
	}

	FString BuildPrototypeStepSignature(const FUMGPrototypeNavigationStep& Step)
	{
		return FString::Printf(
			TEXT("%d|%s|%d|%s|%s|%s|%s|%s|%s|%d|%s|%s|%f"),
			static_cast<int32>(Step.Kind),
			*Step.TargetPageId,
			Step.TargetPageIndex,
			*Step.ExternalUrl,
			*Step.VariableId,
			*Step.VariableValueJson,
			*Step.VariableCollectionId,
			*Step.VariableModeId,
			*Step.ConditionJson,
			Step.JumpTargetIndex,
			*Step.Transition.Type,
			*Step.Transition.Direction,
			Step.Transition.DurationSeconds);
	}

	void MapNodeIdsToTopLevelPage(
		const TSharedPtr<FDesignNode>& Node,
		const FString& PageId,
		TMap<FString, FString>& OutNodeToPageId)
	{
		if (!Node.IsValid() || PageId.IsEmpty())
		{
			return;
		}

		if (!Node->Id.IsEmpty())
		{
			OutNodeToPageId.Add(Node->Id, PageId);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			MapNodeIdsToTopLevelPage(Child, PageId, OutNodeToPageId);
		}
	}

	void CollectPrototypeReservedTokens(
		const TSharedPtr<FDesignNode>& Node,
		TSet<FString>& OutReservedTokens)
	{
		if (!Node.IsValid())
		{
			return;
		}

		if (!Node->Id.IsEmpty())
		{
			OutReservedTokens.Add(Node->Id);
		}
		if (!Node->Name.IsEmpty())
		{
			OutReservedTokens.Add(Node->Name);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			CollectPrototypeReservedTokens(Child, OutReservedTokens);
		}
	}

	uint32 BuildPrototypeInternalTokenHash(const TArray<TSharedPtr<FDesignNode>>& PrototypePages)
	{
		FString Seed;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			if (!PageNode.IsValid())
			{
				continue;
			}

			if (!Seed.IsEmpty())
			{
				Seed += TEXT("|");
			}
			Seed += PageNode->Id.IsEmpty() ? PageNode->Name : PageNode->Id;
		}

		if (Seed.IsEmpty())
		{
			Seed = TEXT("prototype");
		}

		return FCrc::StrCrc32(*Seed);
	}

	FString MakeUniquePrototypeInternalToken(
		TSet<FString>& InOutReservedTokens,
		const uint32 TokenHash,
		const TCHAR* Suffix)
	{
		const FString BaseToken = FString::Printf(
			TEXT("__d2u_prototype_%08x_%s"),
			TokenHash,
			Suffix);
		FString Candidate = BaseToken;
		for (int32 CollisionIndex = 1; InOutReservedTokens.Contains(Candidate); ++CollisionIndex)
		{
			Candidate = FString::Printf(TEXT("%s_%d"), *BaseToken, CollisionIndex);
		}

		InOutReservedTokens.Add(Candidate);
		return Candidate;
	}

	void GatherPrototypeNavigationBindings(
		const TSharedPtr<FDesignNode>& Node,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationBinding>& OutBindings,
		TSet<FString>& OutTriggerIds,
		TSet<FString>& InOutSeenBindingKeys,
		TArray<FString>& Warnings)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const FDesignFigmaPrototypeInteraction& Interaction : Node->FigmaPrototype.Interactions)
		{
			const FString NormalizedTriggerType = NormalizePrototypeTriggerType(Interaction.TriggerType);
			const bool bUsesSupportedTrigger =
				FUMGPrototypeNavigationBinding::IsClickTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsHoverTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsPressTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsDragTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsKeyDownTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsKeyUpTriggerType(NormalizedTriggerType);
			if (!bUsesSupportedTrigger && Interaction.Actions.Num() > 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype trigger '%s' on node '%s' not supported yet"),
					*Interaction.TriggerType,
					*Node->Id));
				continue;
			}

			FUMGPrototypeNavigationBinding Binding;
			Binding.TriggerWidgetId = Node->Id;
			Binding.TriggerWidgetName = Node->Id;
			Binding.TriggerType = NormalizedTriggerType;
			Binding.TriggerJson = Interaction.TriggerJson;
			Binding.TriggerKeyCodes = ParsePrototypeTriggerKeyCodes(Interaction.TriggerJson);
			if (Binding.UsesKeyboardTrigger() && Binding.TriggerKeyCodes.Num() == 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype keyboard trigger '%s' on node '%s' skipped: missing keyCodes"),
					*Interaction.TriggerType,
					*Node->Id));
				continue;
			}

			TSharedPtr<FJsonObject> InteractionObject;
			if (TryParsePrototypeJsonObject(Interaction.RawJson, InteractionObject))
			{
				if (const TArray<TSharedPtr<FJsonValue>>* RawActions = GetPrototypeJsonArray(InteractionObject, TEXT("actions")))
				{
					for (const TSharedPtr<FJsonValue>& RawActionValue : *RawActions)
					{
						const TSharedPtr<FJsonObject> RawActionObject = RawActionValue->AsObject();
						if (!RawActionObject.IsValid())
						{
							continue;
						}

						AppendPrototypeActionStepsFromJsonObject(
							Node,
							RawActionObject,
							NodeToPageId,
							PageIdToIndex,
							Binding.Steps,
							Warnings);
					}
				}
			}
			else
			{
				for (const FDesignFigmaPrototypeAction& Action : Interaction.Actions)
				{
					AppendPrototypeActionStepFromTypedAction(
						Node,
						Action,
						NodeToPageId,
						PageIdToIndex,
						Binding.Steps,
						Warnings);
				}
			}

			if (Binding.Steps.Num() == 0)
			{
				continue;
			}

			FResolvedPrototypeAction Summary;
			if (BuildPrototypeBindingSummary(Binding.Steps, Summary))
			{
				Binding.Action = Summary.Kind;
				Binding.Transition = Summary.Transition;
				if (Summary.Kind == EUMGPrototypeNavigationAction::Navigate
					|| Summary.Kind == EUMGPrototypeNavigationAction::Overlay)
				{
					Binding.TargetPageId = Summary.Payload;
					if (const int32* PageIndex = PageIdToIndex.Find(Summary.Payload))
					{
						Binding.TargetPageIndex = *PageIndex;
					}
				}
				else if (Summary.Kind == EUMGPrototypeNavigationAction::OpenLink)
				{
					Binding.ExternalUrl = Summary.Payload;
				}
			}

			const FString StepSignature = FString::JoinBy(
				Binding.Steps,
				TEXT(";"),
				[](const FUMGPrototypeNavigationStep& Step)
				{
					return BuildPrototypeStepSignature(Step);
				});
			const FString BindingKey = FString::Printf(
				TEXT("%s::%s::%s::%s"),
				*Node->Id,
				*Binding.TriggerType,
				*FString::Join(Binding.TriggerKeyCodes, TEXT(",")),
				*StepSignature);
			if (InOutSeenBindingKeys.Contains(BindingKey))
			{
				continue;
			}

			if (!Binding.IsValid())
			{
				continue;
			}

			OutBindings.Add(Binding);
			OutTriggerIds.Add(Node->Id);
			InOutSeenBindingKeys.Add(BindingKey);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			GatherPrototypeNavigationBindings(
				Child,
				NodeToPageId,
				PageIdToIndex,
				OutBindings,
				OutTriggerIds,
				InOutSeenBindingKeys,
				Warnings);
		}
	}

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

		if (const int32 StartPageIndex = ResolvePageIndexForNodeId(Document.FigmaDocument.PrototypeStartNodeId);
			StartPageIndex != INDEX_NONE)
		{
			return StartPageIndex;
		}

		for (const FDesignFigmaFlowStartingPoint& Flow : Document.FigmaDocument.FlowStartingPoints)
		{
			if (const int32 FlowPageIndex = ResolvePageIndexForNodeId(Flow.NodeId);
				FlowPageIndex != INDEX_NONE)
			{
				return FlowPageIndex;
			}
		}

		return 0;
	}

	FPrototypeNavigationAnalysis BuildPrototypeNavigationAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings)
	{
		FPrototypeNavigationAnalysis Analysis;

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

		TSet<FString> PrototypeReservedTokens;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			CollectPrototypeReservedTokens(PageNode, PrototypeReservedTokens);
		}
		const uint32 PrototypeInternalTokenHash = BuildPrototypeInternalTokenHash(PrototypePages);

		TMap<FString, FString> NodeToPageId;
		TMap<FString, int32> PageIdToIndex;
		if (PrototypePages.Num() > 1)
		{
			Analysis.Data.SwitcherWidgetId = MakeUniquePrototypeInternalToken(
				PrototypeReservedTokens,
				PrototypeInternalTokenHash,
				TEXT("switcher"));
			Analysis.Data.SwitcherWidgetName = Analysis.Data.SwitcherWidgetId;
			for (int32 PageIndex = 0; PageIndex < PrototypePages.Num(); ++PageIndex)
			{
				const TSharedPtr<FDesignNode>& PageNode = PrototypePages[PageIndex];
				if (!PageNode.IsValid() || PageNode->Id.IsEmpty())
				{
					continue;
				}

				Analysis.Data.PageIds.Add(PageNode->Id);
				PageIdToIndex.Add(PageNode->Id, PageIndex);
				MapNodeIdsToTopLevelPage(PageNode, PageNode->Id, NodeToPageId);
			}
		}

		TSet<FString> SeenBindingKeys;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			GatherPrototypeNavigationBindings(
				PageNode,
				NodeToPageId,
				PageIdToIndex,
				Analysis.Data.ClickBindings,
				Analysis.TriggerNodeIds,
				SeenBindingKeys,
				Warnings);
		}

		if (Analysis.Data.HasOverlayBindings())
		{
			Analysis.Data.OverlayHostWidgetId = MakeUniquePrototypeInternalToken(
				PrototypeReservedTokens,
				PrototypeInternalTokenHash,
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
			Analysis.Data.InitialPageIndex = ResolvePrototypeInitialPageIndex(Document, NodeToPageId, PageIdToIndex);
		}
		return Analysis;
	}

	bool ShouldWrapNodeWithPrototypeButton(const TSharedPtr<FDesignNode>& Node, const FConversionState& State)
	{
		return Node.IsValid()
			&& State.PrototypeNavigation.IsValid()
			&& State.PrototypeTriggerNodeIds.Contains(Node->Id);
	}

	FUMGWidgetNode WrapWithPrototypeButton(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Button;
		Button.Type = TEXT("Button");
		Button.Id = Node->Id;
		Button.Name = Node->Id.IsEmpty() ? TEXT("PrototypeButton") : Node->Id;
		Button.Bounds = Widget.Bounds;
		Button.Slot = Widget.Slot;
		Button.WidgetStyle.BackgroundColor = FLinearColor::Transparent;
		Button.WidgetStyle.bHasBackgroundColor = true;
		Button.WidgetStyle.HoveredColor = FLinearColor::Transparent;
		Button.WidgetStyle.bHasHoveredColor = true;
		Button.WidgetStyle.PressedColor = FLinearColor::Transparent;
		Button.WidgetStyle.bHasPressedColor = true;

		Widget.Id = Node->Id + TEXT("_content");
		Widget.Name = (Node->Name.IsEmpty() ? Node->Id : Node->Name) + TEXT("_content");
		Widget.Bounds.X = 0.0f;
		Widget.Bounds.Y = 0.0f;
		Widget.Slot = FUMGSlotData();
		Button.Children.Add(MoveTemp(Widget));
		return Button;
	}

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
		Switcher.WidgetState.bHasActiveWidgetIndex = true;

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

	FUMGWidgetNode BuildPrototypeRoot(
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
		OverlayHost.WidgetStyle.bHasVisibility = true;

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

	void WarmSharedTextCalibrationResolvedNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context);

	void WarmSharedTextCalibrationRecursive(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || ShouldSkipNode(Node))
		{
			return;
		}

		const FConversionContext ActiveContext = Context.WithNodeTheme(Node);
		if (Node->Type == EDesignNodeType::Ref)
		{
			const TSharedPtr<FDesignNode> ActiveNode = ExpandRefForConversion(Node, ActiveContext);
			if (!ActiveNode.IsValid() || ShouldSkipNode(ActiveNode))
			{
				return;
			}

			WarmSharedTextCalibrationResolvedNode(ActiveNode, ActiveContext.WithNodeTheme(ActiveNode));
			return;
		}

		WarmSharedTextCalibrationResolvedNode(Node, ActiveContext);
	}

	void WarmSharedTextCalibrationResolvedNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || ShouldSkipNode(Node))
		{
			return;
		}

		if (Node->Type == EDesignNodeType::Text
			&& Node->TextAlignVertical != EDesignTextAlignVertical::Top
			&& !Node->Content.Contains(TEXT("\n"))
			&& TextMeasurement::IsWrappingTextMode(Node))
		{
			(void)TextMeasurement::ResolveTextFontSizeForUMG(Node);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			WarmSharedTextCalibrationRecursive(Child, Context);
		}
	}

	void WarmSharedTextCalibration(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		const FConversionContext& Context)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			WarmSharedTextCalibrationRecursive(Node, Context);
		}
	}

}

FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FConversionContext& Context, EDesignLayout ParentLayout = EDesignLayout::None);
FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node);
FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);
FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node, FConversionState& State);
void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
TSharedPtr<FDesignNode> ResolveExpandedChildForConversion(const TSharedPtr<FDesignNode>& Child, const FConversionContext& Context, bool bPreComputeFitContent);
void PreResolveFlowChildren(const TSharedPtr<FDesignNode>& Node, const FUMGWidgetNode& Widget, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);
bool ShouldWrapChildWithSizeBox(const TSharedPtr<FDesignNode>& ParentNode, const TSharedPtr<FDesignNode>& ChildNode, const FUMGWidgetNode& ChildWidget);
void AppendConvertedChildren(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;

	for (const FDesignFigmaSkippedNodeReport& Report : Document.FigmaDiagnostics.SkippedDesignerNodes)
	{
		Result.Warnings.AddUnique(FString::Printf(
			TEXT("Figma %s '%s' (%s) skipped from runtime widget tree [%s]"),
			*Report.NodeType,
			Report.NodeName.IsEmpty() ? TEXT("<unnamed>") : *Report.NodeName,
			Report.NodeId.IsEmpty() ? TEXT("<no-id>") : *Report.NodeId,
			Report.Reason.IsEmpty() ? TEXT("designer_only") : *Report.Reason));
	}

	FConversionState State(Document, Result.Warnings);
	FConversionContext RootContext(State);

	Result.FigmaDocument.ComponentsJson = Document.FigmaDocument.ComponentsJson;
	Result.FigmaDocument.ComponentSetsJson = Document.FigmaDocument.ComponentSetsJson;

	TextMeasurement::ResetCache();

	// 深拷贝 Children，避免通过 TSharedPtr 修改调用者的原始数据
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(FRefExpander::DeepCopyNode(Child));

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	VisibleChildren.Reserve(Children.Num());
	for (const auto& Child : Children)
	{
		if (!ShouldSkipNode(Child))
		{
			VisibleChildren.Add(Child);
		}
	}

	{
		const FPrototypeNavigationAnalysis PrototypeAnalysis =
			BuildPrototypeNavigationAnalysis(Document, VisibleChildren, Result.Warnings);
		State.PrototypeNavigation = PrototypeAnalysis.Data;
		State.PrototypeTriggerNodeIds = PrototypeAnalysis.TriggerNodeIds;
		Result.PrototypeNavigation = PrototypeAnalysis.Data;
	}

	// WidgetName 使用第一个非跳过 frame 的名称，fallback 到 WBP_FromPen
	for (const auto& Child : VisibleChildren)
	{
		if (!Child->bReusable && !Child->Name.IsEmpty())
		{
			Result.WidgetName = FString::Printf(TEXT("WBP_%s"), *Child->Name);
			break;
		}
	}
	if (Result.WidgetName.IsEmpty())
		Result.WidgetName = TEXT("WBP_FromPen");

	// 预计算 FitContent 节点的实际尺寸（从子节点递归推算）
	for (const auto& Child : VisibleChildren)
	{
		SizeEstimator::PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable)
			continue;

		Child->X = 0.0f;
		Child->Y = 0.0f;

		FUMGWidgetNode ChildWidget = ConvertNode(Child, RootContext);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (State.PrototypeNavigation.IsValid())
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Convert: %d top-level widgets → prototype root with %d navigation bindings"),
			RenderWidgets.Num(),
			State.PrototypeNavigation.ClickBindings.Num());
		Result.RootWidget = BuildPrototypeRoot(MoveTemp(RenderWidgets), State.PrototypeNavigation);
		Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
		Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
		return Result;
	}

	if (RenderWidgets.Num() == 1)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Convert: single root '%s' type=%s"), *RenderWidgets[0].Name, *RenderWidgets[0].Type);
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
		Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
		return Result;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Convert: %d top-level widgets → CanvasPanel root"), RenderWidgets.Num());
	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
	Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
	return Result;
}

FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FConversionContext& Context, EDesignLayout ParentLayout)
{
	FRecursionGuard Guard(Context.State.RecursionDepth, TEXT("ConvertNode"));
	if (!Guard.bValid)
		return FUMGWidgetNode();

	if (ShouldSkipNode(Node) && !ShouldKeepStructuralLeafInFlow(Node, ParentLayout))
		return FUMGWidgetNode();

	const FConversionContext ActiveContext = Context.WithNodeTheme(Node);

	if (Node->Type == EDesignNodeType::Ref)
	{
		auto Expanded = ExpandRefForConversion(Node, ActiveContext);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();
		SizeEstimator::PreComputeFitContentSizes(Expanded, Context.State.Document);
		return ConvertNode(Expanded, ActiveContext, ParentLayout);
	}

	FUMGWidgetNode Widget;
	if (ShouldUseFigmaRenderFallback(Node))
	{
		Widget = CreateFigmaRenderFallbackWidget(Node);
		Widget.Id = Node->Id;
		Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;
		Widget.Bounds.X = Node->X;
		Widget.Bounds.Y = Node->Y;
		Widget.Bounds.W = Node->Width.Value;
		Widget.Bounds.H = Node->Height.Value;

		const FString FallbackOffsetX = GetSourceExtension(Node, TEXT("figma.renderFallbackOffsetX"));
		const FString FallbackOffsetY = GetSourceExtension(Node, TEXT("figma.renderFallbackOffsetY"));
		const FString FallbackWidth = GetSourceExtension(Node, TEXT("figma.renderFallbackWidth"));
		const FString FallbackHeight = GetSourceExtension(Node, TEXT("figma.renderFallbackHeight"));
		if (!FallbackOffsetX.IsEmpty())
		{
			Widget.Bounds.X += FCString::Atof(*FallbackOffsetX);
		}
		if (!FallbackOffsetY.IsEmpty())
		{
			Widget.Bounds.Y += FCString::Atof(*FallbackOffsetY);
		}
		if (!FallbackWidth.IsEmpty())
		{
			Widget.Bounds.W = FCString::Atof(*FallbackWidth);
		}
		if (!FallbackHeight.IsEmpty())
		{
			Widget.Bounds.H = FCString::Atof(*FallbackHeight);
		}

		Widget.Figma = BuildFigmaNodeData(Node);

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' -> Figma render fallback image"), *Widget.Name);
		return Widget;
	}

	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		Widget = CreateTextWidget(Node);
		if (IsSourceNodeType(Node, TEXT("TEXT_PATH")))
		{
			const FString NodeLabel = Node->Name.IsEmpty() ? Node->Id : Node->Name;
			Context.State.Warnings.Add(FString::Printf(
				TEXT("Figma TEXT_PATH '%s' → TextBlock (text-on-path 降级为普通文本)"),
				*NodeLabel));
		}
		break;
	case EDesignNodeType::IconFont:
		Widget = CreateIconFontWidget(Node, Context.State);
		break;
	case EDesignNodeType::Frame:
	case EDesignNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Rectangle:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Ellipse:
		{
			float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
			float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
			bool bHasGradientFill = Node->Fill.Type == EDesignFillType::Gradient;
			if (!bHasGradientFill)
			{
				for (const FDesignFill& Fill : Node->Fills)
				{
					if (Fill.Type == EDesignFillType::Gradient)
					{
						bHasGradientFill = true;
						break;
					}
				}
			}
			const bool bHasVectorRasterFallback = bHasGradientFill && Node->Metadata.Contains(TEXT("figma.vectorRasterFallback"));
			// 有尺寸、有颜色填充、无描边 → 生成椭圆 SVG path，走栅格化
			// 有描边时走 fallback（Border 支持 OutlineThickness，Image 不支持）
			bool bHasStroke = Node->Stroke.HasStroke();
			if (W > 0 && H > 0 && ((Node->Fill.IsColor() && Node->Fill.bEnabled && !bHasStroke) || bHasVectorRasterFallback))
			{
				float Rx = W / 2.0f, Ry = H / 2.0f;
				Node->Geometry = FString::Printf(
					TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
					Ry, Rx, Ry, W, Ry, Rx, Ry, Ry);
				Widget = PathWidgetBuilder::CreatePathWidget(Node);
			}
			else
			{
				// Fallback: 圆角近似
				Widget = CreateContainerWidget(Node);
				if (W > 0 && H > 0)
				{
					FDesignCornerRadius EllipseRadius;
					EllipseRadius.bUniform = true;
					EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
					FVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
				}
			}
		}
		break;
	case EDesignNodeType::Polygon:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
		}
		else
		{
			Widget = CreateContainerWidget(Node);
			UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
			Context.State.Warnings.Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		}
		break;
	case EDesignNodeType::Path:
		Widget = PathWidgetBuilder::CreatePathWidget(Node);
		break;
	case EDesignNodeType::Line:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
		}
		else
		{
			// 无 Geometry 时用 Width/Height 构造水平线段
			float W = FMath::Max(1.0f, Node->Width.Value);
			float H = FMath::Max(1.0f, Node->Height.Value);
			Node->Geometry = FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), H * 0.5f, W, H * 0.5f);
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
		}
		break;
	default:
		Widget = CreateContainerWidget(Node);
		break;
	}

	Widget.Id = Node->Id;
	Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;
	Widget.Bounds.X = Node->X;
	Widget.Bounds.Y = Node->Y;
	Widget.Bounds.W = Node->Width.Value;
	Widget.Bounds.H = Node->Height.Value;

	UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
		*Widget.Name, *Widget.Id, *Widget.Type,
		Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
		(int32)Node->Layout, Node->Children.Num());

	FVisualMapper::ApplyVisualProperties(Widget, Node);
	if (Widget.Type == TEXT("Border")
		&& !HasWidgetStringProperty(Widget, TEXT("BrushColor"))
		&& !HasWidgetStringProperty(Widget, TEXT("TexturePath"))
		&& !HasWidgetStringProperty(Widget, TEXT("MaterialRef")))
	{
		// Stroke-only / no-fill Border should keep transparent interior.
		// UBorder default brush is opaque white, which can cover siblings.
		Widget.Brush.BrushColor = FLinearColor::Transparent;
		Widget.Brush.bHasBrushColor = true;
		if (HasWidgetStringProperty(Widget, TEXT("OutlineThickness")))
		{
			// Box + WhiteSquareTexture can still flash an opaque interior in UBorder.
			// RoundedBox with zero radii keeps the interior transparent while preserving the outline.
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.bHasDrawAs = true;
		}
	}

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	GatherVisibleChildrenForConversion(Node, VisibleChildren);
	if (Node->Layout == EDesignLayout::Grid && NeedsGridPanel(VisibleChildren))
	{
		Widget.Type = TEXT("GridPanel");
	}
	PreResolveFlowChildren(Node, Widget, VisibleChildren, ActiveContext);
	AppendConvertedChildren(Widget, Node, VisibleChildren, ActiveContext);

	const bool bHasResolvedGridGap = Node->Layout == EDesignLayout::Grid
		&& (ResolveGridColumnGap(Node) > KINDA_SMALL_NUMBER
			|| ResolveGridRowGap(Node) > KINDA_SMALL_NUMBER);
	if (Node->Gap > 0 || bHasResolvedGridGap)
	{
		if (Node->Layout == EDesignLayout::Wrap)
		{
			const float CrossAxisGap = Node->FigmaLayout.bHasCounterAxisSpacing
				? Node->FigmaLayout.CounterAxisSpacing
				: Node->Gap;
			Widget.WidgetStyle.InnerSlotPadding = FVector2D(Node->Gap, CrossAxisGap);
			Widget.WidgetStyle.bHasInnerSlotPadding = true;
		}
		else if (Node->Layout == EDesignLayout::Grid)
		{
			if (Widget.Type == TEXT("UniformGridPanel"))
			{
				const float ColumnGap = ResolveGridColumnGap(Node);
				const float RowGap = ResolveGridRowGap(Node);
				Widget.WidgetStyle.SlotPadding =
					FMargin(ColumnGap * 0.5f, RowGap * 0.5f, ColumnGap * 0.5f, RowGap * 0.5f);
				Widget.WidgetStyle.bHasSlotPadding =
					ColumnGap > KINDA_SMALL_NUMBER || RowGap > KINDA_SMALL_NUMBER;
			}
		}
		else
		{
			FLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);
		}

		if (Node->Layout == EDesignLayout::Grid
			&& Widget.Type == TEXT("UniformGridPanel")
			&& !Widget.WidgetStyle.bHasSlotPadding)
		{
			Widget.WidgetStyle.SlotPadding = FMargin(Node->Gap, Node->Gap, Node->Gap, Node->Gap);
			Widget.WidgetStyle.bHasSlotPadding = true;
		}
	}

	if (Node->JustifyContent != EDesignJustifyContent::Start)
	{
		// Determine the container's main-axis size mode for Spacer suppression.
		// ParentLayout tells us if this container sits inside a CanvasPanel.
		const EDesignSizeMode MainAxisMode = (Node->Layout == EDesignLayout::Horizontal)
			? Node->Width.Mode : Node->Height.Mode;
		FLayoutMapper::ApplyJustifyContent(Widget.Children, Node->JustifyContent, MainAxisMode, ParentLayout);
	}

	const bool bPlaceholderUsesDirectImage = TryApplyPlaceholderSurfaceTint(Widget, Node);
	if (ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, HasVisualFill(Node), Node->CornerRadius.HasRadius(), Node->Stroke.HasStroke());
		Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}
	else if (FLayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
	{
		FLayoutMapper::ApplyPadding(Widget, Node->Padding);
	}

	Widget = ApplyVisualEffectWrappers(MoveTemp(Widget), Node);
	if (ShouldWrapNodeWithPrototypeButton(Node, Context.State))
	{
		Widget = WrapWithPrototypeButton(MoveTemp(Widget), Node);
	}
	Widget.Figma = BuildFigmaNodeData(Node);

	return Widget;
}

FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	if (Node.IsValid())
	{
		const FString& OverflowDirection = Node->FigmaPrototype.OverflowDirection;
		if (OverflowDirection.Equals(TEXT("VERTICAL_SCROLLING"), ESearchCase::IgnoreCase))
		{
			Widget.Type = TEXT("ScrollBox");
			Widget.Properties.Add(TEXT("Orientation"), TEXT("Vertical"));
			Widget.Properties.Add(TEXT("ScrollBarVisibility"), TEXT("Hidden"));
		}
		else if (OverflowDirection.Equals(TEXT("HORIZONTAL_SCROLLING"), ESearchCase::IgnoreCase))
		{
			Widget.Type = TEXT("ScrollBox");
			Widget.Properties.Add(TEXT("Orientation"), TEXT("Horizontal"));
			Widget.Properties.Add(TEXT("ScrollBarVisibility"), TEXT("Hidden"));
		}
	}
	return Widget;
}

FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	constexpr TCHAR RichTextStyleRowsPropertyKey[] = TEXT("Design2UMG.RichTextStyleRows");

	struct FResolvedRichTextRun
	{
		FString RowName;
		FString Content;
		FString FontFamily;
		FString TypefaceFontName;
		float FontSize = 0.0f;
		float DesignFontSize = 0.0f;
		float LetterSpacing = 0.0f;
		bool bHasFontSize = false;
		bool bHasLetterSpacing = false;
		bool bUnderline = false;
		bool bStrikethrough = false;
		bool bHasColor = false;
		FString Color;
		FString Href;
	};

	struct FGeneratedRichTextContent
	{
		FString Markup;
		FString StyleRowsJson;
		FString SingleHref;
		bool bHasStyleRows = false;
	};

	const auto EscapeRichTextText = [](const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Escaped;
	};

	const auto EscapeRichTextAttribute = [&EscapeRichTextText](const FString& Value)
	{
		FString Escaped = EscapeRichTextText(Value);
		Escaped.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		Escaped.ReplaceInline(TEXT("'"), TEXT("&apos;"));
		return Escaped;
	};

	const auto ResolveFontAssetPath = [](const FString& FontFamily)
	{
		if (FontFamily.IsEmpty())
		{
			return FString();
		}

		const FString AssetPath = FFontResolver::FindFontAssetPath(FontFamily);
		return AssetPath.IsEmpty() ? FontFamily : AssetPath;
	};

	const auto ResolveTypefaceName = [](const FString& FontFamily, const FString& FontWeight)
	{
		const FString EffectiveWeight = FontWeight.IsEmpty() ? TEXT("normal") : FontWeight;
		FString Typeface = FFontResolver::FindBestTypeface(FontFamily, EffectiveWeight);
		if (Typeface.IsEmpty())
		{
			Typeface = FDesignTypeMapper::MapFontWeight(EffectiveWeight).ToString();
		}
		return Typeface;
	};

	const auto TextRunNeedsRichText = [](const FDesignTextRun& Run)
	{
		return !Run.FontFamily.IsEmpty()
			|| Run.bHasFontSize
			|| !Run.FontWeight.IsEmpty()
			|| !Run.FontStyle.IsEmpty()
			|| Run.bHasLineHeight
			|| Run.bHasLetterSpacing
			|| Run.bHasUnderline
			|| Run.bHasStrikethrough
			|| !Run.Href.IsEmpty();
	};

	const auto ShouldUseRichTextBlock = [&TextRunNeedsRichText](const TSharedPtr<FDesignNode>& InNode)
	{
		if (!InNode.IsValid())
		{
			return false;
		}

		if (!InNode->Href.IsEmpty())
		{
			return true;
		}

		for (const FDesignTextRun& Run : InNode->TextRuns)
		{
			if (TextRunNeedsRichText(Run))
			{
				return true;
			}
		}

		return false;
	};

	const auto BuildResolvedRichTextRun =
		[&ResolveFontAssetPath, &ResolveTypefaceName](const TSharedPtr<FDesignNode>& InNode, const FDesignTextRun* Run, int32 RunIndex, const float ResolvedNodeFontSize)
	{
		FResolvedRichTextRun Result;
		Result.RowName = FString::Printf(TEXT("d2u_run_%d"), RunIndex);
		Result.Content = Run ? Run->Content : InNode->Content;

		const FString EffectiveFontFamily = (Run && !Run->FontFamily.IsEmpty()) ? Run->FontFamily : InNode->FontFamily;
		Result.FontFamily = ResolveFontAssetPath(EffectiveFontFamily);
		Result.TypefaceFontName = ResolveTypefaceName(
			EffectiveFontFamily,
			(Run && !Run->FontWeight.IsEmpty()) ? Run->FontWeight : InNode->FontWeight);

		const float EffectiveDesignFontSize = (Run && Run->bHasFontSize) ? Run->FontSize : InNode->FontSize;
		if (EffectiveDesignFontSize > 0.0f)
		{
			Result.DesignFontSize = EffectiveDesignFontSize;
			Result.FontSize = (Run && Run->bHasFontSize && InNode->FontSize > KINDA_SMALL_NUMBER && ResolvedNodeFontSize > KINDA_SMALL_NUMBER)
				? (Run->FontSize * (ResolvedNodeFontSize / InNode->FontSize))
				: ((Run && Run->bHasFontSize) ? Run->FontSize : ResolvedNodeFontSize);
			Result.bHasFontSize = Result.FontSize > 0.0f;
		}

		const bool bHasNodeColor = InNode->Fill.Type == EDesignFillType::Color;
		if (bHasNodeColor)
		{
			Result.Color = Design2UMGWidgetNodeAccess::SerializeWidgetColor(FUMGValueParser::ParseColor(InNode->Fill.SolidColor));
			Result.bHasColor = !Result.Color.IsEmpty();
		}

		const bool bRunUnderline = Run ? (Run->bHasUnderline ? Run->bUnderline : InNode->bUnderline) : InNode->bUnderline;
		Result.bStrikethrough = Run ? (Run->bHasStrikethrough ? Run->bStrikethrough : InNode->bStrikethrough) : InNode->bStrikethrough;
		Result.Href = (Run && !Run->Href.IsEmpty()) ? Run->Href : InNode->Href;
		Result.bUnderline = bRunUnderline || !Result.Href.IsEmpty();

		const float EffectiveLetterSpacing = (Run && Run->bHasLetterSpacing) ? Run->LetterSpacing : InNode->LetterSpacing;
		const bool bHasLetterSpacing = (Run && Run->bHasLetterSpacing) || InNode->LetterSpacing != 0.0f;
		if (bHasLetterSpacing)
		{
			Result.LetterSpacing = EffectiveLetterSpacing;
			Result.bHasLetterSpacing = true;
		}

		return Result;
	};

	const auto BuildRichTextContent =
		[&BuildResolvedRichTextRun, &EscapeRichTextAttribute, &EscapeRichTextText](const TSharedPtr<FDesignNode>& InNode)
	{
		FGeneratedRichTextContent Result;
		if (!InNode.IsValid())
		{
			return Result;
		}

		const float ResolvedNodeFontSize = TextMeasurement::ResolveTextFontSizeForUMG(InNode);
		TArray<FResolvedRichTextRun> Runs;
		if (InNode->TextRuns.Num() > 0)
		{
			for (int32 RunIndex = 0; RunIndex < InNode->TextRuns.Num(); ++RunIndex)
			{
				Runs.Add(BuildResolvedRichTextRun(InNode, &InNode->TextRuns[RunIndex], RunIndex, ResolvedNodeFontSize));
			}
		}
		else
		{
			Runs.Add(BuildResolvedRichTextRun(InNode, nullptr, 0, ResolvedNodeFontSize));
		}

		TSharedRef<FJsonObject> StyleRowsObject = MakeShared<FJsonObject>();
		TSet<FString> UniqueHrefs;
		for (const FResolvedRichTextRun& Run : Runs)
		{
			Result.Markup += FString::Printf(TEXT("<%s"), *Run.RowName);
			if (!Run.Href.IsEmpty())
			{
				Result.Markup += FString::Printf(TEXT(" href=\"%s\""), *EscapeRichTextAttribute(Run.Href));
				UniqueHrefs.Add(Run.Href);
			}
			Result.Markup += TEXT(">");
			Result.Markup += EscapeRichTextText(Run.Content);
			Result.Markup += TEXT("</>");

			TSharedRef<FJsonObject> RowObject = MakeShared<FJsonObject>();
			if (!Run.FontFamily.IsEmpty())
			{
				RowObject->SetStringField(TEXT("FontFamily"), Run.FontFamily);
			}
			if (!Run.TypefaceFontName.IsEmpty())
			{
				RowObject->SetStringField(TEXT("TypefaceFontName"), Run.TypefaceFontName);
			}
			if (Run.bHasFontSize)
			{
				RowObject->SetNumberField(TEXT("FontSize"), Run.FontSize);
				RowObject->SetNumberField(TEXT("DesignFontSize"), Run.DesignFontSize > 0.0f ? Run.DesignFontSize : Run.FontSize);
			}
			if (Run.bHasLetterSpacing)
			{
				RowObject->SetNumberField(TEXT("LetterSpacing"), Run.LetterSpacing);
			}
			if (Run.bHasColor)
			{
				RowObject->SetStringField(TEXT("Color"), Run.Color);
			}
			if (Run.bUnderline)
			{
				RowObject->SetBoolField(TEXT("Underline"), true);
			}
			if (Run.bStrikethrough)
			{
				RowObject->SetBoolField(TEXT("Strikethrough"), true);
			}
			if (!Run.Href.IsEmpty())
			{
				RowObject->SetStringField(TEXT("Href"), Run.Href);
			}
			StyleRowsObject->SetObjectField(Run.RowName, RowObject);
		}

		if (UniqueHrefs.Num() == 1)
		{
			for (const FString& UniqueHref : UniqueHrefs)
			{
				Result.SingleHref = UniqueHref;
				break;
			}
		}

		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result.StyleRowsJson);
		Result.bHasStyleRows = FJsonSerializer::Serialize(StyleRowsObject, Writer);
		if (!Result.bHasStyleRows)
		{
			Result.StyleRowsJson.Empty();
		}
		return Result;
	};

	const bool bUseRichTextBlock = ShouldUseRichTextBlock(Node);
	const FGeneratedRichTextContent RichTextContent = bUseRichTextBlock ? BuildRichTextContent(Node) : FGeneratedRichTextContent();
	Widget.Type = bUseRichTextBlock ? TEXT("RichTextBlock") : TEXT("TextBlock");
	Widget.Text.Text = bUseRichTextBlock ? RichTextContent.Markup : TextMeasurement::PrepareRuntimeTextContent(Node->Content);
	Widget.Text.bHasText = true;
	ApplyTextProperties(Widget, Node);
	if (bUseRichTextBlock && RichTextContent.bHasStyleRows)
	{
		Widget.Properties.Add(RichTextStyleRowsPropertyKey, RichTextContent.StyleRowsJson);
	}
	if (!RichTextContent.SingleHref.IsEmpty())
	{
		Widget.WidgetStyle.ToolTipText = RichTextContent.SingleHref;
		Widget.WidgetStyle.bHasToolTipText = true;
	}
	return Widget;
}

void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	if (ResolvedFontSize > 0)
	{
		Widget.TextStyle.FontSize = ResolvedFontSize;
		Widget.TextStyle.SourceFontSize = Node->FontSize;
	}

	if (!Node->FontFamily.IsEmpty())
	{
		FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
		Widget.Text.FontFamily = AssetPath.IsEmpty() ? Node->FontFamily : AssetPath;
		Widget.Text.bHasFontFamily = true;
	}

	if (!Node->FontWeight.IsEmpty())
	{
		FString Typeface = FFontResolver::FindBestTypeface(Node->FontFamily, Node->FontWeight);
		if (Typeface.IsEmpty())
			Typeface = FDesignTypeMapper::MapFontWeight(Node->FontWeight).ToString();
		Widget.Text.TypefaceFontName = Typeface;
		Widget.Text.bHasTypefaceFontName = true;
	}

	if (!Node->FontStyle.IsEmpty())
	{
		Widget.Text.FontStyle = Node->FontStyle;
		Widget.Text.bHasFontStyle = true;
	}

	Widget.Text.Justification =
		Node->TextAlign == EDesignTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EDesignTextAlign::Right ? TEXT("Right") : TEXT("Left");
	Widget.Text.bHasJustification = true;

	const float ResolvedWrapTextAt = TextMeasurement::ResolveTextWrapAt(Node, 0.0f);
	const bool bNeedsKnownWidthRuntimeWrap =
		Node->TextGrowth == EDesignTextGrowth::Auto
		&& Node->Width.IsFill()
		&& Node->Width.Value > 0.0f
		&& !HasNodeMetadataFlag(Node, TEXT("figma.text.truncate"));
	if (Node->TextAlign == EDesignTextAlign::Justify)
	{
		Widget.Text.bAutoWrapText = true;
		Widget.Text.bHasAutoWrapText = true;
		if (ResolvedWrapTextAt > 0.0f)
		{
			Widget.Text.WrapTextAt = ResolvedWrapTextAt;
			Widget.Text.bHasWrapTextAt = true;
		}
	}
	else if (bNeedsKnownWidthRuntimeWrap && ResolvedWrapTextAt > 0.0f)
	{
		Widget.Text.bAutoWrapText = true;
		Widget.Text.bHasAutoWrapText = true;
		Widget.Text.WrapTextAt = ResolvedWrapTextAt;
		Widget.Text.bHasWrapTextAt = true;
	}

	const bool bApplyRuntimeLineHeight =
		TextMeasurement::ShouldApplyExplicitLineHeight(Node, ResolvedFontSize)
		|| (Node->LineHeight > 0.0f
			&& Node->FontSize >= 24.0f
			&& !Node->Content.Contains(TEXT("\n"))
			&& !TextMeasurement::IsWrappingTextMode(Node));
	if (bApplyRuntimeLineHeight)
	{
		Widget.Text.LineHeightPercentage = Node->LineHeight;
		Widget.Text.bHasLineHeightPercentage = true;
	}

	if (Node->LetterSpacing != 0)
	{
		Widget.TextStyle.LetterSpacing = Node->LetterSpacing;
		Widget.TextStyle.bHasLetterSpacing = true;
	}

	if (Node->bUnderline)
	{
		Widget.Text.bUnderline = true;
		Widget.Text.bHasUnderline = true;
	}
	if (Node->bStrikethrough)
	{
		Widget.Text.bStrikethrough = true;
		Widget.Text.bHasStrikethrough = true;
	}
	if (!Node->Href.IsEmpty())
	{
		Widget.Text.Href = Node->Href;
		Widget.Text.bHasHref = true;
	}

	switch (Node->TextGrowth)
	{
	case EDesignTextGrowth::FixedWidth:
	case EDesignTextGrowth::FixedWidthHeight:
		if (HasNodeMetadataFlag(Node, TEXT("figma.text.truncate")))
		{
			Widget.Render.Clipping = TEXT("ClipToBounds");
			Widget.Render.bHasClipping = true;
			break;
		}

		Widget.Text.bAutoWrapText = true;
		Widget.Text.bHasAutoWrapText = true;
		if (ResolvedWrapTextAt > 0.0f)
		{
			Widget.Text.WrapTextAt = ResolvedWrapTextAt;
			Widget.Text.bHasWrapTextAt = true;
		}
		if (Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight)
		{
			Widget.Render.Clipping = TEXT("ClipToBounds");
			Widget.Render.bHasClipping = true;
		}
		break;
	default:
		break;
	}

	if (Node->TextAlignVertical != EDesignTextAlignVertical::Top)
	{
		Widget.Text.TextAlignVertical =
			Node->TextAlignVertical == EDesignTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom");
		Widget.Text.bHasTextAlignVertical = true;

		// textAlignVertical is an internal text-box alignment hint. It must not
		// move the outer layout slot, but fixed-height text still needs internal
		// padding so the runtime TextBlock approximates the authored vertical
		// centering/bottom alignment.
		if (Node->Height.Value > 0.0f)
		{
			const FVector2D MeasuredTextSize = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
			const float ContentHeight = MeasuredTextSize.Y;
			const float SlackHeight = Node->Height.Value - ContentHeight;
			if (ContentHeight > 0.0f && SlackHeight > 0.5f)
			{
				FMargin InternalPadding = Widget.WidgetStyle.Padding;
				if (Node->TextAlignVertical == EDesignTextAlignVertical::Middle)
				{
					const float SplitPadding = SlackHeight * 0.5f;
					InternalPadding.Top += SplitPadding;
					InternalPadding.Bottom += SplitPadding;
				}
				else
				{
					InternalPadding.Top += SlackHeight;
				}

				Widget.WidgetStyle.Padding = InternalPadding;
				Widget.WidgetStyle.bHasPadding = true;
			}
		}
	}

}

FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node, FConversionState& State)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	const auto ResolveIconTypefaceWeightToken = [](const TSharedPtr<FDesignNode>& InNode, const bool bUsingFallbackFontAsset)
	{
		if (!InNode.IsValid())
		{
			return FString(TEXT("Regular"));
		}

		const FString NormalizedFontWeight = InNode->FontWeight.TrimStartAndEnd();
		const bool bHasExplicitNodeFontWeight =
			!NormalizedFontWeight.IsEmpty()
			&& !NormalizedFontWeight.Equals(TEXT("normal"), ESearchCase::IgnoreCase)
			&& !NormalizedFontWeight.Equals(TEXT("regular"), ESearchCase::IgnoreCase)
			&& !NormalizedFontWeight.Equals(TEXT("400"), ESearchCase::IgnoreCase);
		if (bHasExplicitNodeFontWeight)
		{
			return NormalizedFontWeight;
		}

		// Fallback non-icon fonts (for example the default Roboto asset used when an
		// icon family is missing) should not inherit Material Symbols weight tokens.
		// Real icon font assets still honor the authored icon weight.
		return bUsingFallbackFontAsset
			? FString(TEXT("Regular"))
			: FString::FromInt(FMath::Clamp(InNode->IconWeight, 100, 900));
	};

	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	FString AssetPath;
	FString ResolvedIconGlyph;
	bool bUsedDefaultIconPath = false;
	bool bUsedResolverPath = false;
	if (Settings && !Node->IconFontFamily.IsEmpty())
	{
		AssetPath = Settings->FindIconFontAssetPath(Node->IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			AssetPath = Settings->DefaultIconFontAssetPath;
			bUsedDefaultIconPath = !AssetPath.IsEmpty();
		}
	}

	if (!AssetPath.IsEmpty())
	{
		FString ValidAssetPath;
		if (TryNormalizeValidFontAssetPath(AssetPath, ValidAssetPath))
		{
			AssetPath = MoveTemp(ValidAssetPath);
		}
		else
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font path is stale or invalid for family '%s': %s"),
				*Node->IconFontFamily,
				*AssetPath);
			if (!Node->IconFontFamily.IsEmpty())
			{
				State.PrunedInvalidMappings.Add(Node->IconFontFamily);
			}
			AssetPath.Empty();
			bUsedDefaultIconPath = false;
		}
	}

	// Fallback: resolve icon family through general font resolver.
	// This avoids "Last resort fallback font" when icon font has been auto-imported
	// but IconFontAssetMap is not explicitly configured.
	if (AssetPath.IsEmpty() && !Node->IconFontFamily.IsEmpty())
	{
		FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(Node->IconFontFamily);
		FString ValidResolvedPath;
		if (TryNormalizeValidFontAssetPath(ResolvedAssetPath, ValidResolvedPath))
		{
			AssetPath = MoveTemp(ValidResolvedPath);
			bUsedResolverPath = true;
		}
	}

	// If family mapping is missing but we found a usable path, persist mapping once.
	const bool bNeedsPersistMapping = (bUsedDefaultIconPath || bUsedResolverPath)
		&& !Node->IconFontFamily.IsEmpty();
	if (bNeedsPersistMapping)
	{
		State.PendingIconFontMappings.Add(Node->IconFontFamily, AssetPath);
		UE_LOG(LogDesign2UMG, Display,
			TEXT("Queued icon font family mapping '%s' -> '%s'%s"),
			*Node->IconFontFamily,
			*AssetPath,
			bUsedResolverPath ? TEXT(" via FontResolver") : TEXT(""));
	}

	if (!AssetPath.IsEmpty())
	{
		Widget.Text.FontFamily = AssetPath;
		Widget.Text.bHasFontFamily = true;
		ResolvedIconGlyph = FFontResolver::FindKnownIconGlyph(Node->IconFontFamily, Node->IconFontName);
		const bool bUsingFallbackFontAsset = bUsedDefaultIconPath;
		FString WeightToken = ResolveIconTypefaceWeightToken(Node, bUsingFallbackFontAsset);
		FString Typeface = FFontResolver::FindBestTypeface(Node->IconFontFamily, WeightToken);
		if (Typeface.IsEmpty())
		{
			Typeface = FDesignTypeMapper::MapFontWeight(WeightToken).ToString();
		}
		if (!Typeface.IsEmpty())
		{
			Widget.Text.TypefaceFontName = Typeface;
			Widget.Text.bHasTypefaceFontName = true;
		}
	}
	else if (!Node->IconFontFamily.IsEmpty())
	{
		if (!State.WarnedFamilies.Contains(Node->IconFontFamily))
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("IconFont '%s': font family '%s' has no asset mapping"),
				*Node->IconFontName, *Node->IconFontFamily);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font '%s' (family '%s') has no configured asset mapping. Configure it in Editor Preferences > Plugins > Design2UMG."),
				*Node->IconFontName, *Node->IconFontFamily);
			State.WarnedFamilies.Add(Node->IconFontFamily);
		}
	}

	// 图标名称即连字文本（如 "dashboard" 在 Material Symbols 中渲染为图标）
	Widget.Text.IconName = Node->IconFontName;
	Widget.Text.bHasIconName = true;
	FString IconText = !ResolvedIconGlyph.IsEmpty()
		? ResolvedIconGlyph
		: FFontResolver::FindKnownIconGlyph(Node->IconFontFamily, Node->IconFontName);
	if (IconText.IsEmpty())
	{
		IconText = Node->IconFontName;
	}
	if (AssetPath.IsEmpty() && Settings && !Settings->MissingIconFontPlaceholder.IsEmpty())
	{
		IconText = Settings->MissingIconFontPlaceholder;
	}
	if (!IconText.IsEmpty())
	{
		Widget.Text.Text = IconText;
		Widget.Text.bHasText = true;
	}

	if (Node->FontSize > 0)
	{
		Widget.TextStyle.FontSize = Node->FontSize;
		Widget.TextStyle.SourceFontSize = Node->FontSize;
	}

	if (Node->Fill.Type == EDesignFillType::Color)
	{
		Widget.Text.Color = FUMGValueParser::ParseColor(Node->Fill.SolidColor);
		Widget.Text.bHasColor = true;
	}

	return Widget;
}

TSharedPtr<FDesignNode> ResolveExpandedChildForConversion(
	const TSharedPtr<FDesignNode>& Child,
	const FConversionContext& Context,
	bool bPreComputeFitContent)
{
	TSharedPtr<FDesignNode> ExpandedChild = ExpandRefForConversion(Child, Context);
	if (!ExpandedChild.IsValid())
		return Child;
	if (bPreComputeFitContent && ExpandedChild != Child)
		SizeEstimator::PreComputeFitContentSizes(ExpandedChild, Context.State.Document);
	return ExpandedChild;
}

void PreResolveFlowChildren(
	const TSharedPtr<FDesignNode>& Node,
	const FUMGWidgetNode& Widget,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const FConversionContext& Context)
{
	if (Widget.Bounds.W > 0 && Widget.Bounds.H > 0
		&& (Node->Layout == EDesignLayout::Horizontal || Node->Layout == EDesignLayout::Vertical))
	{
		const float InnerW = Widget.Bounds.W - Node->Padding.Left - Node->Padding.Right;
		const float InnerH = Widget.Bounds.H - Node->Padding.Top - Node->Padding.Bottom;
		const bool bHorizontal = Node->Layout == EDesignLayout::Horizontal;
		TArray<FChildSizeSnapshot> EffectiveChildren;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
			EffectiveChildren.Add(BuildChildSizeSnapshot(Child, Context.State.Document));

		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
		const float TotalGap = EffectiveChildren.Num() > 1 ? Node->Gap * (EffectiveChildren.Num() - 1) : 0.0f;
		for (const FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
		{
			const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
			if (MainSize.IsFill())
				++FillCount;
			else
				FixedMainSum += MainSize.Value;

			const FDesignSize& RawMain = bHorizontal ? ChildSnapshot.RawWidth : ChildSnapshot.RawHeight;
			const FDesignSize& RawCross = bHorizontal ? ChildSnapshot.RawHeight : ChildSnapshot.RawWidth;
			const FDesignSize& EffectiveCross = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
			UE_LOG(LogDesign2UMG, Verbose, TEXT("PreResolveFill Child '%s': ref=%d rawMain=%s(%.0f) effMain=%s(%.0f) rawCross=%s(%.0f) effCross=%s(%.0f)"),
				ChildSnapshot.Child.IsValid() ? *ChildSnapshot.Child->Id : TEXT("<invalid>"),
				ChildSnapshot.bUsedExpandedRef ? 1 : 0,
				ToSizeModeLogString(RawMain.Mode), RawMain.Value,
				ToSizeModeLogString(MainSize.Mode), MainSize.Value,
				ToSizeModeLogString(RawCross.Mode), RawCross.Value,
				ToSizeModeLogString(EffectiveCross.Mode), EffectiveCross.Value);
		}

		if (FillCount > 0)
		{
			const float AvailableMain = (bHorizontal ? InnerW : InnerH) - FixedMainSum - TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailableMain / FillCount);
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
				if (MainSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, bHorizontal, FillMain);
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name, bHorizontal ? TEXT("H") : TEXT("V"),
				InnerW, InnerH, FixedMainSum, FillCount, FillMain);
		}
		else
		{
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}
		}
	}

	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		if (Child->Width.IsFit() || Child->Height.IsFit())
		{
			const auto HasExplicitFitDimension = [](const FDesignSize& Size)
			{
				return Size.IsFit() && Size.Value > 0.0f;
			};
			const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForFlow(Child);

			const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, 0.0f);
			const TSharedPtr<FDesignNode> EstimateChild = ResolveExpandedChildForConversion(Child, Context, false);
			const FVector2D Estimate = SizeEstimator::EstimateNodeSize(EstimateChild, ChildContentWidth);
			if (!(bLockExplicitFit && HasExplicitFitDimension(Child->Width)))
			{
				SizeEstimator::PromoteFitNodeDimension(Child, true, Estimate.X);
			}
			if (!(bLockExplicitFit && HasExplicitFitDimension(Child->Height)))
			{
				SizeEstimator::PromoteFitNodeDimension(Child, false, Estimate.Y);
			}
			if (Estimate.X > 0 || Estimate.Y > 0)
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
					*Child->Name, Estimate.X, Estimate.Y);
			}
		}
	}
}

bool ShouldWrapChildWithSizeBox(
	const TSharedPtr<FDesignNode>& ParentNode,
	const TSharedPtr<FDesignNode>& ChildNode,
	const FUMGWidgetNode& ChildWidget)
{
	const bool bHasFixedSize = ChildNode->Width.IsFixed() || ChildNode->Height.IsFixed();
	const bool bHasMinSize = (ChildNode->Width.IsFit() && ChildNode->Width.Value > 0)
		|| (ChildNode->Height.IsFit() && ChildNode->Height.Value > 0);
	const bool bCanvasMixedAxis = ParentNode->Layout == EDesignLayout::None
		&& ((ChildNode->Width.IsFit() && ChildNode->Height.IsFixed())
			|| (ChildNode->Height.IsFit() && ChildNode->Width.IsFixed()));
	const bool bWrapOrGridMeasuredFill =
		(ParentNode->Layout == EDesignLayout::Wrap || ParentNode->Layout == EDesignLayout::Grid)
		&& ((ChildNode->Width.IsFill() && ChildNode->Width.Value > 0.0f)
			|| (ChildNode->Height.IsFill() && ChildNode->Height.Value > 0.0f));
	const bool bWidgetOwnsDesiredSize =
		ParentNode->Layout != EDesignLayout::None
		&& ChildWidget.Type == TEXT("Image")
		&& ChildNode->Children.Num() == 0
		&& (HasWidgetStringProperty(ChildWidget, TEXT("WidthOverride")) || HasWidgetStringProperty(ChildWidget, TEXT("HeightOverride")))
		&& (HasWidgetStringProperty(ChildWidget, TEXT("MaterialRef"))
			|| HasWidgetStringProperty(ChildWidget, TEXT("TexturePath"))
			|| HasWidgetStringProperty(ChildWidget, TEXT("PathGeometry")));
	return ParentNode->Layout == EDesignLayout::None
		? bCanvasMixedAxis
		: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
}

void AppendConvertedChildren(
	FUMGWidgetNode& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const FConversionContext& Context)
{
	const bool bGridPanelParent =
		Node.IsValid()
		&& Node->Layout == EDesignLayout::Grid
		&& Widget.Type.Equals(TEXT("GridPanel"), ESearchCase::CaseSensitive);
	FGridPlacementState GridPlacementState;
	if (bGridPanelParent)
	{
		GridPlacementState.ColumnCount = ResolveGridColumnCount(Node, VisibleChildren);
		GridPlacementState.RowCount = ResolveGridRowCount(Node, VisibleChildren, GridPlacementState.ColumnCount);
	}

	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		TSharedPtr<FDesignNode> EffectiveChild = ResolveExpandedChildForConversion(Child, Context, true);
		FUMGWidgetNode ChildWidget = ConvertNode(EffectiveChild, Context, Node->Layout);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);
		if (Node->Layout == EDesignLayout::Grid)
		{
			if (bGridPanelParent)
			{
				ApplyGridPanelChildSlotData(ChildWidget, Node, EffectiveChild, GridPlacementState);
			}
			else
			{
				if (EffectiveChild->bHasLayoutRow)
				{
					ChildWidget.Slot.Row = FMath::Max(0, EffectiveChild->LayoutRow);
				}
				if (EffectiveChild->bHasLayoutColumn)
				{
					ChildWidget.Slot.Column = FMath::Max(0, EffectiveChild->LayoutColumn);
				}
				ApplyGridChildAlignmentOverrides(ChildWidget, Node, EffectiveChild);
			}
		}
		if (Node->Layout != EDesignLayout::None && !EffectiveChild->bAbsoluteLayoutPositioning)
		{
			ChildWidget.Bounds.X = 0.0f;
			ChildWidget.Bounds.Y = 0.0f;
		}

		if (ShouldWrapChildWithSizeBox(Node, EffectiveChild, ChildWidget))
		{
			const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
			const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
			UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
				*ChildWidget.Name, bHasFixedSize, bHasMinSize,
				EffectiveChild->Width.Value, EffectiveChild->Height.Value);
			Widget.Children.Add(FLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild, Node->Layout));
			continue;
		}

		Widget.Children.Add(MoveTemp(ChildWidget));
	}
}

