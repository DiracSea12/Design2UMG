// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "LayoutMapper.h"
#include "Design2UMGLog.h"
#include "DesignSchema.h"
#include "DesignTypeMapper.h"
#include "ParserSharedSemantics.h"
#include "DesignNodeSemantics.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"

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
			// Deliberately omits the Auto + fill-width growth term that
			// TextMeasurement::IsWrappingTextMode uses. This predicate feeds
			// only ShouldUseFitOverride, i.e. the SizeBox WidthOverride-vs-
			// MinDesired switch for fit-sized children in WrapWithSizeBox.
			// A fill-width auto text wraps at the width its parent's layout
			// grants it (slot fill plus ShouldPreserveMeasuredFillSize and
			// TextMeasurement's parent width hint already carry that), so
			// counting it here would double-process the same signal. Unifying
			// the terms would still need a full visual-regression gate.
			return ParserSharedSemantics::HasFixedWidthTextGrowth(*Node)
				|| ParserSharedSemantics::HasHardNewlineText(*Node);
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

void LayoutMapper::ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
{
	if (ParentLayout == EDesignLayout::Vertical)
	{
		Widget.Slot.HorizontalAlignment = Node->Transform.Width.Mode == EDesignSizeMode::FillContainer
			? EUMGSlotHorizontalAlignment::Fill
			: EUMGSlotHorizontalAlignment::Left;
		Widget.Slot.SizeRule = Node->Transform.Height.Mode == EDesignSizeMode::FillContainer
			? EUMGSlotSizeRule::Fill
			: EUMGSlotSizeRule::Auto;
		if (Widget.Slot.SizeRule == EUMGSlotSizeRule::Fill && Node->LayoutData.LayoutGrow >= 0.0f)
		{
			if (FMath::IsNearlyZero(Node->LayoutData.LayoutGrow))
				Widget.Slot.SizeRule = EUMGSlotSizeRule::Auto;
			else
				Widget.Slot.FillWeight = Node->LayoutData.LayoutGrow;
		}
	}
	else if (ParentLayout == EDesignLayout::Horizontal)
	{
		Widget.Slot.VerticalAlignment = Node->Transform.Height.Mode == EDesignSizeMode::FillContainer
			? EUMGSlotVerticalAlignment::Fill
			: EUMGSlotVerticalAlignment::Top;
		Widget.Slot.SizeRule = Node->Transform.Width.Mode == EDesignSizeMode::FillContainer
			? EUMGSlotSizeRule::Fill
			: EUMGSlotSizeRule::Auto;
		if (Widget.Slot.SizeRule == EUMGSlotSizeRule::Fill && Node->LayoutData.LayoutGrow >= 0.0f)
		{
			if (FMath::IsNearlyZero(Node->LayoutData.LayoutGrow))
				Widget.Slot.SizeRule = EUMGSlotSizeRule::Auto;
			else
				Widget.Slot.FillWeight = Node->LayoutData.LayoutGrow;
		}
	}
	else if (ParentLayout == EDesignLayout::Wrap || ParentLayout == EDesignLayout::Grid)
	{
		Widget.Slot.Padding = FMargin(0.0f);
		Widget.Slot.SizeRule = EUMGSlotSizeRule::Auto;
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
		// FitContent uses AutoSize only when the axis has no explicit pixel value.
		// If the design already provides pixel values, which is common for component instances, keep offsets to preserve the static layout.
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

void LayoutMapper::ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EDesignLayout Layout)
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

void LayoutMapper::ApplyPadding(FUMGWidgetNode& Widget, const FDesignPadding& Padding)
{
	Widget.WidgetStyle.Padding = FMargin(Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
}

void LayoutMapper::ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EDesignJustifyContent Justify, EDesignSizeMode MainAxisMode, EDesignLayout ParentLayout)
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
		if (C.Slot.SizeRule == EUMGSlotSizeRule::Fill)
		{
			bHasFillChild = true;
			break;
		}
	}
	if (bHasFillChild)
		return;

	auto CreateSpacer = [](float FillWeight = 1.0f) -> FUMGWidgetNode {
		FUMGWidgetNode Spacer;
		Spacer.Type = UMGWidgetTypes::Spacer;
		Spacer.bSyntheticJustifySpacer = true;
		Spacer.Slot.SizeRule = EUMGSlotSizeRule::Fill;
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

void LayoutMapper::ApplyAlignItems(FUMGWidgetNode& Widget, EDesignAlignItems Align, EDesignLayout ParentLayout)
{
	if (ParentLayout == EDesignLayout::Vertical)
	{
		if (Widget.Slot.HorizontalAlignment != EUMGSlotHorizontalAlignment::Fill)
		{
			EHorizontalAlignment H = DesignTypeMapper::MapAlignItemsH(Align);
			Widget.Slot.HorizontalAlignment = H == HAlign_Center ? EUMGSlotHorizontalAlignment::Center :
				H == HAlign_Right ? EUMGSlotHorizontalAlignment::Right : EUMGSlotHorizontalAlignment::Left;
		}
	}
	else if (ParentLayout == EDesignLayout::Horizontal)
	{
		if (Widget.Slot.VerticalAlignment != EUMGSlotVerticalAlignment::Fill)
		{
			EVerticalAlignment V = DesignTypeMapper::MapAlignItemsV(Align);
			Widget.Slot.VerticalAlignment = V == VAlign_Center ? EUMGSlotVerticalAlignment::Center :
				V == VAlign_Bottom ? EUMGSlotVerticalAlignment::Bottom : EUMGSlotVerticalAlignment::Top;
		}
	}
}

bool LayoutMapper::HasPadding(const TSharedPtr<FDesignNode>& Node)
{
	return Node.IsValid() &&
		(Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top > 0 || Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right > 0 ||
		 Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom > 0 || Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left > 0);
}

bool LayoutMapper::TryFuseSizeConstraints(FUMGWidgetNode& ChildWidget, const FUMGWidgetNode& SizeBoxWidget)
{
	static const TSet<FString> SupportedWidgetTypes = {
		UMGWidgetTypes::Border,
		UMGWidgetTypes::CanvasPanel,
		UMGWidgetTypes::HorizontalBox,
		UMGWidgetTypes::Image,
		UMGWidgetTypes::Overlay,
		UMGWidgetTypes::RetainerBox,
		UMGWidgetTypes::TextBlock,
		UMGWidgetTypes::VerticalBox,
		UMGWidgetTypes::WrapBox
	};

	if (!SupportedWidgetTypes.Contains(ChildWidget.Type)
		|| (!SizeBoxWidget.WidgetStyle.WidthOverride.IsSet()
			&& !SizeBoxWidget.WidgetStyle.HeightOverride.IsSet()
			&& !SizeBoxWidget.WidgetStyle.MinDesiredWidth.IsSet()
			&& !SizeBoxWidget.WidgetStyle.MinDesiredHeight.IsSet()))
	{
		return false;
	}

	ChildWidget.Type = UMGWidgetTypes::MakeConstrainedTypeName(ChildWidget.Type);
	ChildWidget.WidgetStyle.WidthOverride = SizeBoxWidget.WidgetStyle.WidthOverride;
	ChildWidget.WidgetStyle.HeightOverride = SizeBoxWidget.WidgetStyle.HeightOverride;
	ChildWidget.WidgetStyle.MinDesiredWidth = SizeBoxWidget.WidgetStyle.MinDesiredWidth;
	ChildWidget.WidgetStyle.MinDesiredHeight = SizeBoxWidget.WidgetStyle.MinDesiredHeight;
	return true;
}

FUMGWidgetNode LayoutMapper::WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FDesignNode>& ChildNode, EDesignLayout ParentLayout)
{
	FUMGWidgetNode SizeBox;
	SizeBox.Type = UMGWidgetTypes::SizeBox;
	SizeBox.Id = ChildWidget.Id + TEXT("_sz");
	SizeBox.Name = ChildWidget.Name + TEXT("_sz");
	const bool bUseFitOverride = ShouldUseFitOverride(ChildNode);
	const bool bPreserveMeasuredFillSize = ShouldPreserveMeasuredFillSize(ChildNode, ParentLayout);
	float SourceTextBoxWidth = 0.0f;
	float SourceTextBoxHeight = 0.0f;
	const bool bHasConcreteSourceTextBoxSize =
		Design2UMGCore::DesignNodeSemantics::TryGetConcreteSourceTextBoxSize(ChildNode, SourceTextBoxWidth, SourceTextBoxHeight);

	if (bHasConcreteSourceTextBoxSize)
	{
		SizeBox.WidgetStyle.WidthOverride = SourceTextBoxWidth;
	}
	else if (ChildNode->Transform.Width.IsFixed())
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

	if (bHasConcreteSourceTextBoxSize)
	{
		SizeBox.WidgetStyle.HeightOverride = SourceTextBoxHeight;
	}
	else if (ChildNode->Transform.Height.IsFixed())
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
	if (bHasConcreteSourceTextBoxSize)
	{
		SizeBox.Bounds.W = SourceTextBoxWidth;
		SizeBox.Bounds.H = SourceTextBoxHeight;
	}
	if (TryFuseSizeConstraints(ChildWidget, SizeBox))
	{
		ChildWidget.Bounds = SizeBox.Bounds;
		return MoveTemp(ChildWidget);
	}
	SizeBox.Slot = MoveTemp(ChildWidget.Slot);
	if (ChildWidget.HasSourceData())
	{
		SizeBox.MutableSource() = ChildWidget.Source();
	}
	ChildWidget.Slot = FUMGSlotData();
	SizeBox.Children.Add(MoveTemp(ChildWidget));
	return SizeBox;
}

