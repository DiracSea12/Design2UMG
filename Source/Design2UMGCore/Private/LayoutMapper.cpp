// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "LayoutMapper.h"
#include "DesignSchema.h"
#include "DesignTypeMapper.h"

namespace
{
	bool UsesWrappingText(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text)
		{
			return Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth
				|| Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight
				|| Node->Text.TextAlign == EDesignTextAlign::Justify
				|| Node->Text.Content.Contains(TEXT("\n"));
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (UsesWrappingText(Child))
			{
				return true;
			}
		}

		return false;
	}

	bool ShouldUseFitOverride(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}
		return !UsesWrappingText(Node);
	}

	bool ShouldPreserveMeasuredFillSize(const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (ParentLayout == EDesignLayout::Wrap || ParentLayout == EDesignLayout::Grid)
		{
			return true;
		}

		return ParentLayout != EDesignLayout::None
			&& Node->Type == EDesignNodeType::Text
			&& (Node->Text.TextAlign == EDesignTextAlign::Center || Node->Text.TextAlign == EDesignTextAlign::Right);
	}
}

void FLayoutMapper::ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
{
	if (ParentLayout == EDesignLayout::Vertical)
	{
		Widget.Slot.HorizontalAlignment = Node->Transform.Width.Mode == EDesignSizeMode::FillContainer ? TEXT("Fill") : TEXT("Left");
		Widget.Slot.SizeRule = Node->Transform.Height.Mode == EDesignSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
		if (Widget.Slot.SizeRule == TEXT("Fill") && Node->LayoutData.LayoutGrow >= 0.0f)
		{
			if (FMath::IsNearlyZero(Node->LayoutData.LayoutGrow))
				Widget.Slot.SizeRule = TEXT("Auto");
			else
				Widget.Slot.FillWeight = Node->LayoutData.LayoutGrow;
		}
	}
	else if (ParentLayout == EDesignLayout::Horizontal)
	{
		Widget.Slot.VerticalAlignment = Node->Transform.Height.Mode == EDesignSizeMode::FillContainer ? TEXT("Fill") : TEXT("Top");
		Widget.Slot.SizeRule = Node->Transform.Width.Mode == EDesignSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
		if (Widget.Slot.SizeRule == TEXT("Fill") && Node->LayoutData.LayoutGrow >= 0.0f)
		{
			if (FMath::IsNearlyZero(Node->LayoutData.LayoutGrow))
				Widget.Slot.SizeRule = TEXT("Auto");
			else
				Widget.Slot.FillWeight = Node->LayoutData.LayoutGrow;
		}
	}
	else if (ParentLayout == EDesignLayout::Wrap || ParentLayout == EDesignLayout::Grid)
	{
		Widget.Slot.Padding = FMargin(0.0f);
		Widget.Slot.SizeRule = TEXT("Auto");
	}
	else
	{
		const bool bFillWidth = Node->Transform.Width.Mode == EDesignSizeMode::FillContainer;
		const bool bFillHeight = Node->Transform.Height.Mode == EDesignSizeMode::FillContainer;
		const bool bFillWidthHasFallback = bFillWidth && Node->Transform.Width.Value > 0.0f;
		const bool bFillHeightHasFallback = bFillHeight && Node->Transform.Height.Value > 0.0f;
		const bool bStretchWidth = bFillWidth && !bFillWidthHasFallback;
		const bool bStretchHeight = bFillHeight && !bFillHeightHasFallback;

		if (bStretchWidth || bStretchHeight)
		{
			Widget.Slot.Anchors.Min.X = 0.0f;
			Widget.Slot.Anchors.Max.X = bStretchWidth ? 1.0f : 0.0f;
			Widget.Slot.Anchors.Min.Y = 0.0f;
			Widget.Slot.Anchors.Max.Y = bStretchHeight ? 1.0f : 0.0f;
			Widget.Slot.Alignment.X = 0.0f;
			Widget.Slot.Alignment.Y = 0.0f;
			Widget.Slot.Offsets.Left = Node->Transform.X;
			Widget.Slot.Offsets.Top = Node->Transform.Y;
			Widget.Slot.Offsets.Right = bStretchWidth ? 0.0f : Node->Transform.Width.Value;
			Widget.Slot.Offsets.Bottom = bStretchHeight ? 0.0f : Node->Transform.Height.Value;
			return;
		}

		Widget.Slot.Offsets.Left = Node->Transform.X;
		Widget.Slot.Offsets.Top = Node->Transform.Y;
		Widget.Slot.Offsets.Right = Node->Transform.Width.Value;
		Widget.Slot.Offsets.Bottom = Node->Transform.Height.Value;

		const bool bFitWidth = Node->Transform.Width.Mode == EDesignSizeMode::FitContent;
		const bool bFitHeight = Node->Transform.Height.Mode == EDesignSizeMode::FitContent;
		const bool bFixedWidth = Node->Transform.Width.Mode == EDesignSizeMode::Fixed;
		const bool bFixedHeight = Node->Transform.Height.Mode == EDesignSizeMode::Fixed;
		const bool bFitWidthNeedsAuto = bFitWidth && Node->Transform.Width.Value <= 0.0f;
		const bool bFitHeightNeedsAuto = bFitHeight && Node->Transform.Height.Value <= 0.0f;

		// CanvasPanel fidelity rule:
		// FitContent 只有在该轴没有显式像素值时才走 AutoSize。
		// 若已有设计稿像素值（常见于组件实例），优先保留 offsets 以贴近设计稿静态布局。
		if (bFitWidthNeedsAuto || bFitHeightNeedsAuto)
		{
			Widget.Slot.bAutoSize = true;

			// Mixed-axis: one axis FitContent, the other Fixed.
			// bAutoSize overrides both axes to use DesiredSize, so we must
			// preserve the Fixed axis value in Offsets for the SizeBox wrapper
			// to pick up via WidthOverride / HeightOverride.
			if (bFitWidth && bFixedHeight)
				Widget.Slot.Offsets.Bottom = Node->Transform.Height.Value;
			else if (bFitHeight && bFixedWidth)
				Widget.Slot.Offsets.Right = Node->Transform.Width.Value;
		}
	}
}

void FLayoutMapper::ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EDesignLayout Layout)
{
	if (Gap <= 0 || Children.Num() < 2)
		return;

	for (int32 i = 1; i < Children.Num(); ++i)
	{
		if (Layout == EDesignLayout::Vertical)
			Children[i].Slot.Padding.Top = Gap;
		else if (Layout == EDesignLayout::Horizontal)
			Children[i].Slot.Padding.Left = Gap;
		else if (Layout == EDesignLayout::Wrap)
		{
			Children[i].Slot.Padding.Left = Gap;
			Children[i].Slot.Padding.Top = Gap;
		}
		else if (Layout == EDesignLayout::Grid)
		{
			Children[i].Slot.Padding.Left = Gap;
			Children[i].Slot.Padding.Top = Gap;
		}
	}
}

void FLayoutMapper::ApplyPadding(FUMGWidgetNode& Widget, const FDesignPadding& Padding)
{
	Widget.WidgetStyle.Padding = FMargin(Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
}

void FLayoutMapper::ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EDesignJustifyContent Justify, EDesignSizeMode MainAxisMode, EDesignLayout ParentLayout)
{
	if (Justify == EDesignJustifyContent::Start || Children.Num() == 0)
		return;

	// FitContent container in CanvasPanel: Spacer DesiredSize is (0,0),
	// injecting them collapses the container's DesiredSize to zero.
	if (MainAxisMode == EDesignSizeMode::FitContent && ParentLayout == EDesignLayout::None)
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
	case EDesignJustifyContent::Center:
		Children.Insert(CreateSpacer(), 0);
		Children.Add(CreateSpacer());
		break;
	case EDesignJustifyContent::End:
		Children.Insert(CreateSpacer(), 0);
		break;
	case EDesignJustifyContent::SpaceBetween:
		if (Children.Num() > 1)
		{
			for (int32 i = Children.Num() - 1; i > 0; --i)
				Children.Insert(CreateSpacer(), i);
		}
		break;
	case EDesignJustifyContent::SpaceAround:
		Children.Insert(CreateSpacer(0.5f), 0);
		for (int32 i = Children.Num() - 1; i > 1; --i)
			Children.Insert(CreateSpacer(), i);
		Children.Add(CreateSpacer(0.5f));
		break;
	default:
		break;
	}
}

void FLayoutMapper::ApplyAlignItems(FUMGWidgetNode& Widget, EDesignAlignItems Align, EDesignLayout ParentLayout)
{
	if (ParentLayout == EDesignLayout::Vertical)
	{
		if (Widget.Slot.HorizontalAlignment != TEXT("Fill"))
		{
			EHorizontalAlignment H = FDesignTypeMapper::MapAlignItemsH(Align);
			Widget.Slot.HorizontalAlignment = H == HAlign_Center ? TEXT("Center") :
				H == HAlign_Right ? TEXT("Right") : TEXT("Left");
		}
	}
	else if (ParentLayout == EDesignLayout::Horizontal)
	{
		if (Widget.Slot.VerticalAlignment != TEXT("Fill"))
		{
			EVerticalAlignment V = FDesignTypeMapper::MapAlignItemsV(Align);
			Widget.Slot.VerticalAlignment = V == VAlign_Center ? TEXT("Center") :
				V == VAlign_Bottom ? TEXT("Bottom") : TEXT("Top");
		}
	}
}

bool FLayoutMapper::HasPadding(const TSharedPtr<FDesignNode>& Node)
{
	return Node.IsValid() &&
		(Node->LayoutData.Padding.Top > 0 || Node->LayoutData.Padding.Right > 0 ||
		 Node->LayoutData.Padding.Bottom > 0 || Node->LayoutData.Padding.Left > 0);
}

FUMGWidgetNode FLayoutMapper::WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode, EDesignLayout ParentLayout)
{
	FUMGWidgetNode SizeBox;
	SizeBox.Type = TEXT("SizeBox");
	SizeBox.Id = ChildWidget.Id + TEXT("_sz");
	SizeBox.Name = ChildWidget.Name + TEXT("_sz");
	const bool bUseFitOverride = ShouldUseFitOverride(ChildNode);
	const bool bPreserveMeasuredFillSize = ShouldPreserveMeasuredFillSize(ChildNode, ParentLayout);

	if (ChildNode->Transform.Width.IsFixed())
	{
		SizeBox.WidgetStyle.WidthOverride = ChildNode->Transform.Width.Value;
	}
	else if (ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Width.Value > 0)
	{
		if (bUseFitOverride)
		{
			SizeBox.WidgetStyle.WidthOverride = ChildNode->Transform.Width.Value;
		}
		else
		{
			SizeBox.WidgetStyle.MinDesiredWidth = ChildNode->Transform.Width.Value;
		}
	}
	else if (bPreserveMeasuredFillSize && ChildNode->Transform.Width.IsFill() && ChildNode->Transform.Width.Value > 0.0f)
	{
		SizeBox.WidgetStyle.WidthOverride = ChildNode->Transform.Width.Value;
	}

	if (ChildNode->Transform.Height.IsFixed())
	{
		SizeBox.WidgetStyle.HeightOverride = ChildNode->Transform.Height.Value;
	}
	else if (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Height.Value > 0)
	{
		if (bUseFitOverride)
		{
			SizeBox.WidgetStyle.HeightOverride = ChildNode->Transform.Height.Value;
		}
		else
		{
			SizeBox.WidgetStyle.MinDesiredHeight = ChildNode->Transform.Height.Value;
		}
	}
	else if (bPreserveMeasuredFillSize && ChildNode->Transform.Height.IsFill() && ChildNode->Transform.Height.Value > 0.0f)
	{
		SizeBox.WidgetStyle.HeightOverride = ChildNode->Transform.Height.Value;
	}

	SizeBox.Bounds = ChildWidget.Bounds;
	SizeBox.Slot = MoveTemp(ChildWidget.Slot);
	ChildWidget.Slot = FUMGSlotData();
	SizeBox.Children.Add(MoveTemp(ChildWidget));
	return SizeBox;
}




