// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "WidgetTreeChildBuilder.h"

#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ContentWidget.h"
#include "Components/GridPanel.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"

namespace
{
	bool HasMeaningfulSlotData(const FUMGSlotData& Slot)
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

	bool HasExplicitGridData(const FUMGSlotData& Slot)
	{
		const FUMGSlotData Default;
		return Slot.Row != Default.Row ||
			Slot.Column != Default.Column ||
			Slot.RowSpan != Default.RowSpan ||
			Slot.ColumnSpan != Default.ColumnSpan;
	}

	bool HasValidBounds(const FDesignPixelBounds& Bounds)
	{
		return Bounds.W > 0.0f && Bounds.H > 0.0f;
	}

	struct FAutoGridItem
	{
		int32 Index = INDEX_NONE;
		float Top = 0.0f;
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		float Bottom = 0.0f;
		float Height = 0.0f;
	};

	bool BelongsToLocalRow(const FAutoGridItem& Item, const FAutoGridItem& RowAnchor)
	{
		// Compare against the first item in the row using the smaller local
		// height.  This keeps a single tall/short outlier from changing the
		// threshold for every other row and avoids transitive "chain" merges.
		const float LocalTolerance = FMath::Max(0.01f, 0.5f * FMath::Min(Item.Height, RowAnchor.Height));
		return FMath::Abs(Item.Top - RowAnchor.Top) <= LocalTolerance
			|| FMath::Abs(Item.CenterY - RowAnchor.CenterY) <= LocalTolerance
			|| FMath::Abs(Item.Bottom - RowAnchor.Bottom) <= LocalTolerance;
	}

	bool ShouldAutoAssignGridSlots(const TArray<FUMGWidgetNode>& Children)
	{
		for (const FUMGWidgetNode& Child : Children)
		{
			if (!HasExplicitGridData(Child.Slot) && HasValidBounds(Child.Bounds))
			{
				return true;
			}
		}
		return false;
	}

	void BuildAutoGridSlots(const TArray<FUMGWidgetNode>& Children, TMap<int32, FIntPoint>& OutSlots)
	{
		OutSlots.Reset();

		TSet<int32> ExplicitIndices;
		TSet<FIntPoint> OccupiedCells;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FUMGWidgetNode& Child = Children[Index];
			if (HasExplicitGridData(Child.Slot))
			{
				const FIntPoint Cell(Child.Slot.Row, Child.Slot.Column);
				OutSlots.Add(Index, Cell);
				OccupiedCells.Add(Cell);
				ExplicitIndices.Add(Index);
			}
		}

		TArray<FAutoGridItem> Items;
		Items.Reserve(Children.Num());

		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FUMGWidgetNode& Child = Children[Index];
			if (!HasValidBounds(Child.Bounds))
			{
				continue;
			}

			FAutoGridItem Item;
			Item.Index = Index;
			Item.Top = Child.Bounds.Y;
			Item.CenterX = Child.Bounds.GetCenterX();
			Item.CenterY = Child.Bounds.GetCenterY();
			Item.Bottom = Child.Bounds.Y + Child.Bounds.H;
			Item.Height = Child.Bounds.H;
			Items.Add(Item);
		}

		if (Items.Num() == 0)
		{
			return;
		}

		Items.Sort([](const FAutoGridItem& A, const FAutoGridItem& B)
		{
			if (FMath::IsNearlyEqual(A.CenterY, B.CenterY))
			{
				return A.CenterX < B.CenterX;
			}
			return A.CenterY < B.CenterY;
		});

		TArray<TArray<FAutoGridItem>> Rows;
		TArray<FAutoGridItem> RowAnchors;
		RowAnchors.Reserve(Items.Num());

		for (const FAutoGridItem& Item : Items)
		{
			if (Rows.Num() == 0)
			{
				Rows.Add({ Item });
				RowAnchors.Add(Item);
				continue;
			}

			if (BelongsToLocalRow(Item, RowAnchors.Last()))
			{
				Rows.Last().Add(Item);
				continue;
			}

			Rows.Add({ Item });
			RowAnchors.Add(Item);
		}

		for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
		{
			TArray<FAutoGridItem>& RowItems = Rows[RowIndex];
			RowItems.Sort([](const FAutoGridItem& A, const FAutoGridItem& B)
			{
				return A.CenterX < B.CenterX;
			});

			for (int32 ColIndex = 0; ColIndex < RowItems.Num(); ++ColIndex)
			{
				const FAutoGridItem& Item = RowItems[ColIndex];
				if (ExplicitIndices.Contains(Item.Index))
				{
					continue;
				}

				FIntPoint Cell(RowIndex, ColIndex);
				while (OccupiedCells.Contains(Cell))
				{
					Cell.Y++;
				}
				OutSlots.Add(Item.Index, Cell);
				OccupiedCells.Add(Cell);
			}
		}
	}

	UWidget* CreateChildWidget(
		UWidgetTree* WidgetTree,
		const FUMGWidgetNode& ChildNode,
		UPanelWidget* ParentWidget,
		const FWidgetTreeChildBuildContext& Context)
	{
		return FUMGWidgetUtils::CreateWidgetRecursive(
			WidgetTree,
			ChildNode,
			ParentWidget,
			Context.TextureMap,
			Context.FigmaMetadataRecords,
			Context.ExplicitDesignSize,
			Context.ComponentMappingState);
	}

	void BuildPanelChildren(
		UWidgetTree* WidgetTree,
		UPanelWidget* Panel,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context)
	{
		TMap<int32, FIntPoint> AutoGridSlots;
		if ((Panel->IsA<UGridPanel>() || Panel->IsA<UUniformGridPanel>()) && ShouldAutoAssignGridSlots(Node.Children))
		{
			BuildAutoGridSlots(Node.Children, AutoGridSlots);
		}

		for (int32 ChildIndex = 0; ChildIndex < Node.Children.Num(); ++ChildIndex)
		{
			const FUMGWidgetNode& Child = Node.Children[ChildIndex];
			if (const FIntPoint* SlotIndex = AutoGridSlots.Find(ChildIndex))
			{
				FUMGWidgetNode ChildCopy = Child;
				ChildCopy.Slot.Row = SlotIndex->X;
				ChildCopy.Slot.Column = SlotIndex->Y;
				CreateChildWidget(WidgetTree, ChildCopy, Panel, Context);
				continue;
			}

			CreateChildWidget(WidgetTree, Child, Panel, Context);
		}
	}

	FName ResolveNamedSlotTarget(
		INamedSlotInterface* NamedSlotHost,
		const TArray<FName>& SlotNames,
		const FUMGWidgetNode& ChildNode,
		int32& NextSlotIndex,
		bool& bFallbackAppend)
	{
		bFallbackAppend = false;

		FName TargetSlot = NAME_None;
		const FString FallbackSlotName = ChildNode.WidgetStyle.NamedSlot.Get(FString());
		FString RequestedSlotName = ChildNode.WidgetStyle.SlotName.Get(FString());
		if (RequestedSlotName.IsEmpty())
		{
			RequestedSlotName = FallbackSlotName;
		}

		if (!RequestedSlotName.IsEmpty())
		{
			for (const FName& SlotName : SlotNames)
			{
				if (SlotName.ToString().Equals(RequestedSlotName, ESearchCase::IgnoreCase))
				{
					return SlotName;
				}
			}
		}

		while (NextSlotIndex < SlotNames.Num() && NamedSlotHost->GetContentForSlot(SlotNames[NextSlotIndex]) != nullptr)
		{
			++NextSlotIndex;
		}
		if (NextSlotIndex < SlotNames.Num())
		{
			return SlotNames[NextSlotIndex++];
		}

		if (SlotNames.Num() > 0)
		{
			bFallbackAppend = true;
			return SlotNames.Last();
		}

		return NAME_None;
	}

	void AppendToNamedSlot(
		UWidgetTree* WidgetTree,
		INamedSlotInterface* NamedSlotHost,
		const FUMGWidgetNode& HostNode,
		const FName& SlotName,
		UWidget* ChildWidget,
		const FUMGWidgetNode& ChildNode)
	{
		if (UWidget* Existing = NamedSlotHost->GetContentForSlot(SlotName))
		{
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Existing))
			{
				if (UPanelSlot* NewSlot = Panel->AddChild(ChildWidget))
				{
					FUMGWidgetUtils::ApplySlotProperties(NewSlot, ChildNode.Slot);
				}
				return;
			}

			const FName WrapName = FUMGWidgetUtils::SanitizeWidgetName(
				HostNode.Name + TEXT("_SlotWrap_") + SlotName.ToString(),
				TEXT("SlotWrap"));
			if (UVerticalBox* WrapBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), WrapName))
			{
				WrapBox->AddChild(Existing);
				if (UPanelSlot* NewSlot = WrapBox->AddChild(ChildWidget))
				{
					FUMGWidgetUtils::ApplySlotProperties(NewSlot, ChildNode.Slot);
				}
				NamedSlotHost->SetContentForSlot(SlotName, WrapBox);
			}
			return;
		}

		NamedSlotHost->SetContentForSlot(SlotName, ChildWidget);
	}

	void BuildNamedSlotChildren(
		UWidgetTree* WidgetTree,
		INamedSlotInterface* NamedSlotHost,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context)
	{
		TArray<FName> SlotNames;
		NamedSlotHost->GetSlotNames(SlotNames);

		if (SlotNames.Num() == 0)
		{
			if (Node.Children.Num() > 0)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot host '%s' has no slot names but has %d child widgets."),
					*Node.Name, Node.Children.Num());
			}
			return;
		}

		int32 NextSlotIndex = 0;
		for (const FUMGWidgetNode& ChildNode : Node.Children)
		{
			UWidget* ChildWidget = CreateChildWidget(WidgetTree, ChildNode, nullptr, Context);
			if (!ChildWidget)
			{
				continue;
			}

			bool bFallbackAppend = false;
			const FName TargetSlot = ResolveNamedSlotTarget(
				NamedSlotHost,
				SlotNames,
				ChildNode,
				NextSlotIndex,
				bFallbackAppend);
			if (TargetSlot.IsNone())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot host '%s' has no available slot; child widget '%s' was skipped."),
					*Node.Name, *ChildNode.Name);
				continue;
			}

			if (HasMeaningfulSlotData(ChildNode.Slot))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot child '%s' has slot data that cannot be applied and was ignored (host '%s')."),
					*ChildNode.Name, *Node.Name);
			}

			if (bFallbackAppend)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot host '%s' is full; child widget '%s' was appended to '%s'."),
					*Node.Name, *ChildNode.Name, *TargetSlot.ToString());
			}
			else if (NamedSlotHost->GetContentForSlot(TargetSlot) != nullptr)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot '%s' on '%s' already has content; merging child widget '%s'."),
					*TargetSlot.ToString(), *Node.Name, *ChildNode.Name);
			}

			AppendToNamedSlot(WidgetTree, NamedSlotHost, Node, TargetSlot, ChildWidget, ChildNode);
		}
	}

	void BuildContentWidgetChild(
		UWidgetTree* WidgetTree,
		UContentWidget* Content,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context)
	{
		if (Node.Children.Num() == 0)
		{
			return;
		}

		const FUMGWidgetNode& ChildNode = Node.Children[0];
		if (UWidget* ChildWidget = CreateChildWidget(WidgetTree, ChildNode, nullptr, Context))
		{
			if (UPanelSlot* ContentSlot = Content->SetContent(ChildWidget))
			{
				if (!Content->IsA<USizeBox>())
				{
					FUMGWidgetUtils::ApplySlotProperties(ContentSlot, ChildNode.Slot);
				}
			}

			if (UBorder* Border = Cast<UBorder>(Content))
			{
				Border->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(ChildNode.Slot.HorizontalAlignment));
				Border->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(ChildNode.Slot.VerticalAlignment));
			}
		}

		if (Node.Children.Num() > 1)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ContentWidget '%s' ignoring %d extra children"), *Node.Name, Node.Children.Num() - 1);
		}
	}
}

namespace FWidgetTreeChildBuilder
{
	void BuildChildren(
		UWidgetTree* WidgetTree,
		UWidget* ParentWidget,
		const FUMGWidgetNode& Node,
		const FWidgetTreeChildBuildContext& Context)
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(ParentWidget))
		{
			BuildPanelChildren(WidgetTree, Panel, Node, Context);
			return;
		}

		if (INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(ParentWidget))
		{
			BuildNamedSlotChildren(WidgetTree, NamedSlotHost, Node, Context);
			return;
		}

		if (UContentWidget* Content = Cast<UContentWidget>(ParentWidget))
		{
			BuildContentWidgetChild(WidgetTree, Content, Node, Context);
		}
	}
}
