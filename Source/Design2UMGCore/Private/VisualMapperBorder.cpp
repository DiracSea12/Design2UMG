// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "DesignSchema.h"
#include "VisualMapperInternal.h"
#include "VisualMapperStrokeSupport.h"
#include "LayoutMapper.h"
#include "DesignNodeSemantics.h"
#include "Design2UMGLog.h"
#include "UMGValueParser.h"
#include "UMGWidgetTypes.h"

using VisualMapperInternal::TransferWrapperRenderProperties;

namespace
{
	bool HasOpaqueSolidFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		return PrimaryFill.bEnabled
			&& PrimaryFill.Type == EDesignFillType::Color
			&& !PrimaryFill.SolidColor.IsEmpty()
			&& FUMGValueParser::ParseColor(PrimaryFill.SolidColor).A > KINDA_SMALL_NUMBER;
	}

	bool HasPlaceholderSurfaceChildren(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (Child.IsValid() && Child->LayoutData.bSemanticPlaceholderSurface)
			{
				return true;
			}
		}

		return false;
	}

	bool HasVisualBrushPayload(const FUMGVisualBrushData& Brush)
	{
		return Brush.OutlineCornerRadius.IsSet()
			|| Brush.CornerRadii.IsSet()
			|| Brush.OutlineThickness.IsSet()
			|| Brush.Margin.IsSet()
			|| Brush.DrawAs.IsSet()
			|| Brush.BrushColor.IsSet()
			|| Brush.OutlineColor.IsSet()
			|| Brush.MaterialRef.IsSet()
			|| Brush.MaterialParams.IsSet()
			|| Brush.Shape.IsSet()
			|| Brush.Image.HasAny()
			|| Brush.Gradient.HasAny();
	}

	bool CanElideEmptyWrappedPanel(
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Children.Num() > 0
			|| !UMGWidgetTypes::IsLayoutPanel(Widget.Type)
			|| Widget.Children.Num() > 0
			|| Widget.HasSourceData())
		{
			return false;
		}

		FUMGWidgetStyleData StandaloneStyle = Widget.WidgetStyle;
		// Child-spacing data has no effect once the panel has no children.
		StandaloneStyle.InnerSlotPadding.Reset();
		StandaloneStyle.SlotPadding.Reset();

		return !StandaloneStyle.HasAny()
			&& Widget.TextStyle.FontSize == 0.0f
			&& Widget.TextStyle.SourceFontSize == 0.0f
			&& !Widget.TextStyle.LetterSpacing.IsSet()
			&& !Widget.Text.HasAny()
			&& !HasVisualBrushPayload(Widget.Brush)
			&& !Widget.Effect.HasAny()
			&& !Widget.Path.HasAny()
			&& !Widget.Shadow.HasAny()
			&& !Widget.Render.HasAny()
			&& !Widget.WidgetState.HasAny()
			&& !Widget.ScrollBox.HasAny();
	}

	void TransferBorderBackgroundProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		bool bHasBackgroundProperty = false;
		if (Widget.WidgetStyle.WidthOverride.IsSet())
		{
			Border.WidgetStyle.WidthOverride = Widget.WidgetStyle.WidthOverride;
			Widget.WidgetStyle.WidthOverride.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.HeightOverride.IsSet())
		{
			Border.WidgetStyle.HeightOverride = Widget.WidgetStyle.HeightOverride;
			Widget.WidgetStyle.HeightOverride.Reset();
			bHasBackgroundProperty = true;
		}

		bHasBackgroundProperty = bHasBackgroundProperty
			|| Border.Brush.BrushColor.IsSet()
			|| Border.Brush.Image.TexturePath.IsSet()
			|| Border.Brush.MaterialRef.IsSet()
			|| Border.Brush.Gradient.HasAny();

		if (!bHasBackgroundProperty)
		{
			if (!Border.Brush.BrushColor.IsSet())
			{
				Border.Brush.BrushColor = FLinearColor::Transparent;
			}
		}
	}

	void TransferBorderStrokeProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		// Direct typed reads with descriptor-identical presence rules.
		if (!Border.Brush.DrawAs.IsSet() && Widget.Brush.DrawAs.IsSet())
		{
			Border.Brush.DrawAs = Widget.Brush.DrawAs.GetValue();
		}
		if (!Border.Brush.OutlineCornerRadius.IsSet() && Widget.Brush.OutlineCornerRadius.IsSet())
		{
			Border.Brush.OutlineCornerRadius = Widget.Brush.OutlineCornerRadius;
		}
		if (!Border.Brush.CornerRadii.IsSet() && Widget.Brush.CornerRadii.IsSet())
		{
			Border.Brush.CornerRadii = Widget.Brush.CornerRadii;
		}
		if (!Border.Brush.OutlineThickness.IsSet() && Widget.Brush.OutlineThickness.IsSet())
		{
			Border.Brush.OutlineThickness = Widget.Brush.OutlineThickness.GetValue();
		}
		if (!Border.Brush.OutlineColor.IsSet() && Widget.Brush.OutlineColor.IsSet())
		{
			Border.Brush.OutlineColor = Widget.Brush.OutlineColor.GetValue();
		}

		if (Widget.WidgetStyle.StrokePadding.IsSet())
		{
			Border.WidgetStyle.StrokePadding = Widget.WidgetStyle.StrokePadding;
			Widget.WidgetStyle.StrokePadding.Reset();
		}
	}

	void ApplyOutsideStrokePaddingToBorder(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FMargin Margin = FMargin(0.0f);
		bool bHasStrokeMargin = false;
		if (Widget.WidgetStyle.StrokeMargin.IsSet())
		{
			Margin = Widget.WidgetStyle.StrokeMargin.GetValue();
			bHasStrokeMargin = true;
		}

		if (!bHasStrokeMargin)
		{
			return;
		}

		const FMargin Outset(
			FMath::Max(0.0f, -Margin.Left),
			FMath::Max(0.0f, -Margin.Top),
			FMath::Max(0.0f, -Margin.Right),
			FMath::Max(0.0f, -Margin.Bottom));
		if (Outset.Left > 0.0f || Outset.Top > 0.0f || Outset.Right > 0.0f || Outset.Bottom > 0.0f)
		{
			FMargin Combined = Outset;
			if (Border.WidgetStyle.StrokePadding.IsSet())
			{
				const FMargin ExistingPadding = Border.WidgetStyle.StrokePadding.GetValue();
				Combined.Left += ExistingPadding.Left;
				Combined.Top += ExistingPadding.Top;
				Combined.Right += ExistingPadding.Right;
				Combined.Bottom += ExistingPadding.Bottom;
			}

			Border.WidgetStyle.StrokePadding = Combined;
		}

		Widget.WidgetStyle.StrokeMargin.Reset();
	}

	void TransferBorderBoxShadowProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		if (Widget.Shadow.BoxShadows.Num() > 0)
		{
			Border.Shadow.BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
		}
	}

	void PrepareWrappedContentWidget(FUMGWidgetNode& Widget)
	{
		constexpr TCHAR InnerSuffix[] = TEXT("_inner");
		Widget.Id = Widget.Id + InnerSuffix;
		Widget.Name = Widget.Name + InnerSuffix;
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

	bool IsPromotableFlowPanelType(const FString& Type)
	{
		return Type == UMGWidgetTypes::HorizontalBox
			|| Type == UMGWidgetTypes::VerticalBox;
	}

	bool HasEnabledInnerShadow(const FDesignNode& Node)
	{
		for (const FDesignEffect& Effect : Node.Visual.Effects)
		{
			if (Effect.bEnabled
				&& Effect.Type == EDesignEffectType::Shadow
				&& Effect.Shadow.bInner)
			{
				return true;
			}
		}
		return false;
	}

	bool CanPromoteSingleChildFlowContent(
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FUMGWidgetNode& Border,
		int32& OutContentIndex)
	{
		OutContentIndex = INDEX_NONE;
		TArray<TSharedPtr<FDesignNode>> VisibleChildren;
		if (Node.IsValid())
		{
			if (Node->Children.Num() == 1)
			{
				VisibleChildren.Add(Node->Children[0]);
			}
			else
			{
				for (const TSharedPtr<FDesignNode>& Child : Node->Children)
				{
					if (!Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(Child))
					{
						VisibleChildren.Add(Child);
					}
				}
			}
		}
		if (!Node.IsValid()
			|| VisibleChildren.Num() != 1
			|| (Node->LayoutData.Layout != EDesignLayout::Horizontal
				&& Node->LayoutData.Layout != EDesignLayout::Vertical)
			|| !IsPromotableFlowPanelType(Widget.Type)
			|| !VisibleChildren[0].IsValid()
			|| VisibleChildren[0]->LayoutData.bAbsoluteLayoutPositioning
			|| HasEnabledInnerShadow(*Node)
			|| Widget.WidgetStyle.HasAny()
			|| Widget.HasSourceData())
		{
			return false;
		}

		// Elide only redundant positioned-child panels. Overflowing Auto children need
		// the panel allocation because Border alignment would clamp them.
		const bool bHorizontal = Node->LayoutData.Layout == EDesignLayout::Horizontal;
		const FDesignSize& ChildMainSize = bHorizontal
			? VisibleChildren[0]->Transform.Width : VisibleChildren[0]->Transform.Height;
		const FDesignSize& MainSize = bHorizontal ? Node->Transform.Width : Node->Transform.Height;
		const FMargin ContentPadding = Border.WidgetStyle.Padding.Get(FMargin(0.f))
			+ Border.WidgetStyle.StrokePadding.Get(FMargin(0.f));
		const float AvailableMainSize = MainSize.Value - (bHorizontal
			? ContentPadding.Left + ContentPadding.Right : ContentPadding.Top + ContentPadding.Bottom);
		// FitContent grows; an unresolved Fill size is not a zero-sized bound.
		const bool bHasMainAxisLimit = MainSize.IsFixed() || (MainSize.IsFill() && MainSize.Value > 0.f);
		if (VisibleChildren[0]->Transform.HasExplicitPosition()
			&& bHasMainAxisLimit && !ChildMainSize.IsFill() && ChildMainSize.Value > AvailableMainSize)
		{
			return false;
		}
		if (Node->LayoutData.JustifyContent == EDesignJustifyContent::Start)
		{
			if (Widget.Children.Num() != 1)
			{
				return false;
			}
			OutContentIndex = 0;
			return true;
		}

		// A fill child already suppresses LayoutMapper's synthetic justify spacers;
		// the flow panel therefore contributes no additional main-axis behavior.
		if (Widget.Children.Num() == 1 && Widget.Children[0].Slot.SizeRule == EUMGSlotSizeRule::Fill)
		{
			OutContentIndex = 0;
			return true;
		}

		// LayoutMapper intentionally skips justify spacers for a FitContent flow
		// node when it is placed directly in an absolute parent. With one Auto
		// child, that output has no remaining flow-panel duty; preserve the
		// requested alignment on the Border slot instead.
		const bool bSingleFitContentChild =
			Widget.Children.Num() == 1
			&& Widget.Children[0].Slot.SizeRule == EUMGSlotSizeRule::Auto
			&& ((Node->LayoutData.Layout == EDesignLayout::Horizontal && Node->Transform.Width.IsFit())
				|| (Node->LayoutData.Layout == EDesignLayout::Vertical && Node->Transform.Height.IsFit()));
		const bool bJustifyWouldUseLeadingSpacer =
			Node->LayoutData.JustifyContent == EDesignJustifyContent::Center
			|| Node->LayoutData.JustifyContent == EDesignJustifyContent::End
			|| Node->LayoutData.JustifyContent == EDesignJustifyContent::SpaceAround;
		if (bSingleFitContentChild && bJustifyWouldUseLeadingSpacer)
		{
			OutContentIndex = 0;
			return true;
		}

		auto IsSyntheticJustifySpacer = [](const FUMGWidgetNode& Candidate)
		{
			return Candidate.bSyntheticJustifySpacer;
		};

		int32 NonSpacerIndex = INDEX_NONE;
		int32 SpacerCount = 0;
		for (int32 Index = 0; Index < Widget.Children.Num(); ++Index)
		{
			if (IsSyntheticJustifySpacer(Widget.Children[Index]))
			{
				++SpacerCount;
			}
			else if (NonSpacerIndex == INDEX_NONE)
			{
				NonSpacerIndex = Index;
			}
			else
			{
				return false;
			}
		}

		const int32 ExpectedSpacerCount =
			Node->LayoutData.JustifyContent == EDesignJustifyContent::End ? 1 :
			(Node->LayoutData.JustifyContent == EDesignJustifyContent::Center
				|| Node->LayoutData.JustifyContent == EDesignJustifyContent::SpaceAround) ? 2 : 0;
		if (NonSpacerIndex == INDEX_NONE || SpacerCount != ExpectedSpacerCount)
		{
			return false;
		}

		if (ExpectedSpacerCount == 1)
		{
			if (NonSpacerIndex != 1 || !IsSyntheticJustifySpacer(Widget.Children[0]))
			{
				return false;
			}
		}
		else if (ExpectedSpacerCount == 2)
		{
			if (NonSpacerIndex != 1
				|| !IsSyntheticJustifySpacer(Widget.Children[0])
				|| !IsSyntheticJustifySpacer(Widget.Children[2]))
			{
				return false;
			}
		}

		OutContentIndex = NonSpacerIndex;
		return true;
	}

	void PreparePromotedBorderContent(
		FUMGWidgetNode& Child,
		const EDesignLayout ParentLayout,
		const EDesignJustifyContent JustifyContent)
	{
		if (Child.Slot.SizeRule == EUMGSlotSizeRule::Fill)
		{
			if (ParentLayout == EDesignLayout::Horizontal)
			{
				Child.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
			}
			else
			{
				Child.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
			}
		}
		else if (ParentLayout == EDesignLayout::Horizontal)
		{
			// HorizontalBox ignores horizontal alignment for its child; Border does not.
			// Translate the one-child justify result to the Border content slot.
			Child.Slot.HorizontalAlignment =
				JustifyContent == EDesignJustifyContent::Center || JustifyContent == EDesignJustifyContent::SpaceAround
					? EUMGSlotHorizontalAlignment::Center
					: JustifyContent == EDesignJustifyContent::End
						? EUMGSlotHorizontalAlignment::Right
						: EUMGSlotHorizontalAlignment::Left;
		}
		else
		{
			// VerticalBox ignores vertical alignment for its child when the size is Auto.
			Child.Slot.VerticalAlignment =
				JustifyContent == EDesignJustifyContent::Center || JustifyContent == EDesignJustifyContent::SpaceAround
					? EUMGSlotVerticalAlignment::Center
					: JustifyContent == EDesignJustifyContent::End
						? EUMGSlotVerticalAlignment::Bottom
						: EUMGSlotVerticalAlignment::Top;
		}
		Child.Slot.SizeRule = EUMGSlotSizeRule::Auto;
		Child.Slot.FillWeight = 1.0f;
	}
}

FUMGWidgetNode VisualMapper::WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Border;
	Border.Type = UMGWidgetTypes::Border;
	Border.Id = Widget.Id;
	Border.Name = Widget.Name;
	Border.Bounds = Widget.Bounds;

	// Transfer the visual brush to Border while keeping TextStyle on the original widget.
	Border.Brush = Widget.Brush;
	Widget.Brush = FUMGVisualBrushData();

	TransferBorderBackgroundProperties(Border, Widget);

	if (LayoutMapper::HasPadding(Node))
		LayoutMapper::ApplyPadding(Border, Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}));

	TransferBorderStrokeProperties(Border, Widget);
	if (Node->Visual.Stroke.HasStroke()
		&& Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside
		&& Node->LayoutData.ExplicitPadding.IsSet()
		&& (Node->LayoutData.Layout == EDesignLayout::Horizontal
			|| Node->LayoutData.Layout == EDesignLayout::Vertical))
	{
		// Authored flow padding is measured from the box, not its centered outline.
		Border.WidgetStyle.StrokePadding.Reset();
	}

	// Absolute positions measured from expanded visual bounds already include the
	// outside stroke inset. Border padding would apply an additional offset.
	if (Node->LayoutData.Layout == EDesignLayout::None
		&& Node->LayoutData.bChildPositionsRelativeToVisualBounds)
	{
		Widget.WidgetStyle.StrokeMargin.Reset();
	}
	else
	{
		ApplyOutsideStrokePaddingToBorder(Border, Widget);
	}
	TransferWrapperRenderProperties(Border, Widget, Node);
	TransferBorderBoxShadowProperties(Border, Widget);
	int32 PromotedContentIndex = INDEX_NONE;
	const bool bPromoteSingleChildFlowContent =
		CanPromoteSingleChildFlowContent(Widget, Node, Border, PromotedContentIndex);
	if (bPromoteSingleChildFlowContent)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("VisualMapper: '%s' → promote single flow child into border"), *Widget.Name);
		FUMGWidgetNode PromotedContent = MoveTemp(Widget.Children[PromotedContentIndex]);
		Widget.Children.Reset();
		PreparePromotedBorderContent(PromotedContent, Node->LayoutData.Layout, Node->LayoutData.JustifyContent);
		Border.Children.Add(MoveTemp(PromotedContent));
	}
	else if (CanElideEmptyWrappedPanel(Widget, Node))
	{
		// The wrapped panel has no children and no source identity, so it can vanish without a trace.
		UE_LOG(LogDesign2UMG, Verbose, TEXT("VisualMapper: '%s' → elide empty wrapped panel"), *Widget.Name);
	}
	else
	{
		PrepareWrappedContentWidget(Widget);
		Border.Children.Add(MoveTemp(Widget));
	}

	const bool bNeedsExplicitStroke =
		(Node->Visual.Stroke.HasStroke()
			&& Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside
			&& !Node->Visual.CornerRadius.HasRadius()
			&& !HasPlaceholderSurfaceChildren(Node))
		|| (VisualMapperStrokeSupport::NeedsExplicitSideStroke(Node)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& !HasOpaqueSolidFill(Node))
		|| (Node.IsValid()
			&& Node->Visual.Stroke.HasStroke()
			&& !Node->Visual.CornerRadius.HasRadius()
			&& VisualMapperStrokeSupport::HasUniformRectStroke(Node->Visual.Stroke)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& VisualMapperStrokeSupport::HasAnyConvertedChildStrokeCoverage(Border));

	if (bNeedsExplicitStroke)
	{
		return VisualMapperStrokeSupport::WrapWithExplicitSideStroke(MoveTemp(Border), Node);
	}

	return Border;
}
