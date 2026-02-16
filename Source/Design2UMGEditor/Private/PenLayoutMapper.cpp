// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenLayoutMapper.h"
#include "PenTypeMapper.h"

void FPenLayoutMapper::ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node, EPenLayout ParentLayout)
{
	if (ParentLayout == EPenLayout::Vertical)
	{
		Widget.Slot.HorizontalAlignment = Node->Width.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Left");
		Widget.Slot.SizeRule = Node->Height.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
	}
	else if (ParentLayout == EPenLayout::Horizontal)
	{
		Widget.Slot.VerticalAlignment = Node->Height.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Top");
		Widget.Slot.SizeRule = Node->Width.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
	}
	else
	{
		const bool bFillWidth = Node->Width.Mode == EPenSizeMode::FillContainer;
		const bool bFillHeight = Node->Height.Mode == EPenSizeMode::FillContainer;

		if (bFillWidth || bFillHeight)
		{
			Widget.Slot.Anchors.Min.X = 0.0f;
			Widget.Slot.Anchors.Max.X = bFillWidth ? 1.0f : 0.0f;
			Widget.Slot.Anchors.Min.Y = 0.0f;
			Widget.Slot.Anchors.Max.Y = bFillHeight ? 1.0f : 0.0f;
			Widget.Slot.Alignment.X = 0.0f;
			Widget.Slot.Alignment.Y = 0.0f;
			Widget.Slot.Offsets.Left = Node->X;
			Widget.Slot.Offsets.Top = Node->Y;
			Widget.Slot.Offsets.Right = bFillWidth ? 0.0f : Node->Width.Value;
			Widget.Slot.Offsets.Bottom = bFillHeight ? 0.0f : Node->Height.Value;
			return;
		}

		Widget.Slot.Offsets.Left = Node->X;
		Widget.Slot.Offsets.Top = Node->Y;
		Widget.Slot.Offsets.Right = Node->Width.Value;
		Widget.Slot.Offsets.Bottom = Node->Height.Value;

		const bool bFitWidth = Node->Width.Mode == EPenSizeMode::FitContent;
		const bool bFitHeight = Node->Height.Mode == EPenSizeMode::FitContent;
		const bool bFixedWidth = Node->Width.Mode == EPenSizeMode::Fixed;
		const bool bFixedHeight = Node->Height.Mode == EPenSizeMode::Fixed;

		if (bFitWidth || bFitHeight)
		{
			Widget.Slot.bAutoSize = true;

			// Mixed-axis: one axis FitContent, the other Fixed.
			// bAutoSize overrides both axes to use DesiredSize, so we must
			// preserve the Fixed axis value in Offsets for the SizeBox wrapper
			// to pick up via WidthOverride / HeightOverride.
			if (bFitWidth && bFixedHeight)
				Widget.Slot.Offsets.Bottom = Node->Height.Value;
			else if (bFitHeight && bFixedWidth)
				Widget.Slot.Offsets.Right = Node->Width.Value;
		}
	}
}

void FPenLayoutMapper::ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout)
{
	if (Gap <= 0 || Children.Num() < 2)
		return;

	for (int32 i = 1; i < Children.Num(); ++i)
	{
		if (Layout == EPenLayout::Vertical)
			Children[i].Slot.Padding.Top = Gap;
		else if (Layout == EPenLayout::Horizontal)
			Children[i].Slot.Padding.Left = Gap;
	}
}

void FPenLayoutMapper::ApplyPadding(FUMGWidgetNode& Widget, const FPenPadding& Padding)
{
	Widget.Properties.Add(TEXT("Padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
		Padding.Left, Padding.Top, Padding.Right, Padding.Bottom));
}

void FPenLayoutMapper::ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify, EPenSizeMode MainAxisMode, EPenLayout ParentLayout)
{
	if (Justify == EPenJustifyContent::Start || Children.Num() == 0)
		return;

	// FitContent container in CanvasPanel: Spacer DesiredSize is (0,0),
	// injecting them collapses the container's DesiredSize to zero.
	if (MainAxisMode == EPenSizeMode::FitContent && ParentLayout == EPenLayout::None)
		return;

	bool bHasFillChild = false;
	for (const auto& C : Children)
	{
		if (C.Slot.SizeRule == TEXT("Fill"))
		{
			bHasFillChild = true;
			break;
		}
	}
	if (bHasFillChild)
		return;

	auto CreateSpacer = [](float FillWeight = 1.0f) -> FUMGWidgetNode {
		FUMGWidgetNode Spacer;
		Spacer.Type = TEXT("Spacer");
		Spacer.Slot.SizeRule = TEXT("Fill");
		if (!FMath::IsNearlyEqual(FillWeight, 1.0f))
			Spacer.Slot.FillWeight = FillWeight;
		return Spacer;
	};

	switch (Justify)
	{
	case EPenJustifyContent::Center:
		Children.Insert(CreateSpacer(), 0);
		Children.Add(CreateSpacer());
		break;
	case EPenJustifyContent::End:
		Children.Insert(CreateSpacer(), 0);
		break;
	case EPenJustifyContent::SpaceBetween:
		if (Children.Num() > 1)
		{
			for (int32 i = Children.Num() - 1; i > 0; --i)
				Children.Insert(CreateSpacer(), i);
		}
		break;
	case EPenJustifyContent::SpaceAround:
		Children.Insert(CreateSpacer(0.5f), 0);
		for (int32 i = Children.Num() - 1; i > 1; --i)
			Children.Insert(CreateSpacer(), i);
		Children.Add(CreateSpacer(0.5f));
		break;
	default:
		break;
	}
}

void FPenLayoutMapper::ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout)
{
	if (ParentLayout == EPenLayout::Vertical)
	{
		if (Widget.Slot.HorizontalAlignment != TEXT("Fill"))
		{
			EHorizontalAlignment H = FPenTypeMapper::MapAlignItemsH(Align);
			Widget.Slot.HorizontalAlignment = H == HAlign_Center ? TEXT("Center") :
				H == HAlign_Right ? TEXT("Right") : TEXT("Left");
		}
	}
	else if (ParentLayout == EPenLayout::Horizontal)
	{
		if (Widget.Slot.VerticalAlignment != TEXT("Fill"))
		{
			EVerticalAlignment V = FPenTypeMapper::MapAlignItemsV(Align);
			Widget.Slot.VerticalAlignment = V == VAlign_Center ? TEXT("Center") :
				V == VAlign_Bottom ? TEXT("Bottom") : TEXT("Top");
		}
	}
}

bool FPenLayoutMapper::HasPadding(const TSharedPtr<FPenNode>& Node)
{
	return Node.IsValid() &&
		(Node->Padding.Top > 0 || Node->Padding.Right > 0 ||
		 Node->Padding.Bottom > 0 || Node->Padding.Left > 0);
}

FUMGWidgetNode FPenLayoutMapper::WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FPenNode>& ChildNode)
{
	FUMGWidgetNode SizeBox;
	SizeBox.Type = TEXT("SizeBox");
	SizeBox.Id = ChildWidget.Id + TEXT("_sz");
	SizeBox.Name = ChildWidget.Name + TEXT("_sz");

	if (ChildNode->Width.IsFixed())
		SizeBox.Properties.Add(TEXT("WidthOverride"), FString::SanitizeFloat(ChildNode->Width.Value));
	else if (ChildNode->Width.IsFit() && ChildNode->Width.Value > 0)
		SizeBox.Properties.Add(TEXT("MinDesiredWidth"), FString::SanitizeFloat(ChildNode->Width.Value));

	if (ChildNode->Height.IsFixed())
		SizeBox.Properties.Add(TEXT("HeightOverride"), FString::SanitizeFloat(ChildNode->Height.Value));
	else if (ChildNode->Height.IsFit() && ChildNode->Height.Value > 0)
		SizeBox.Properties.Add(TEXT("MinDesiredHeight"), FString::SanitizeFloat(ChildNode->Height.Value));

	SizeBox.Bounds = ChildWidget.Bounds;
	SizeBox.Slot = MoveTemp(ChildWidget.Slot);
	ChildWidget.Slot = FUMGSlotData();
	SizeBox.Children.Add(MoveTemp(ChildWidget));
	return SizeBox;
}
