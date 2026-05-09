// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "Design2UMGGridLayout.h"
#include "Design2UMGNodeWidgetFactory.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGConverterFlowSupport.h"
#include "Design2UMGConverterInternal.h"
#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGSourceMetadata.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "SizeEstimator.h"
#include "SharedCoreSemantics.h"
#include "TextMeasurement.h"
#include "RefExpander.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

namespace
{
	using namespace Design2UMGConverterInternal;
	namespace VisualSupport = Design2UMGConverterVisualSupport;

	FConversionSettingsSnapshot BuildConversionSettingsSnapshot()
	{
		FConversionSettingsSnapshot Snapshot;
		if (const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get())
		{
			Snapshot.PlaceholderHatchStyle = Settings->ResolvePlaceholderHatchStyle();
			Snapshot.VisualMapperOptions.LuminosityFitOpacityCap = Settings->ResolveLuminosityFitOpacityCap();
			Snapshot.IconFont = Design2UMGTextWidgetBuilder::BuildIconFontSettingsSnapshot(Settings);
		}
		else
		{
			Snapshot.PlaceholderHatchStyle = UDesign2UMGSettings::FResolvedPlaceholderHatchStyle();
			Snapshot.VisualMapperOptions.LuminosityFitOpacityCap = UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
		}
		return Snapshot;
	}

	struct FResolvedPrototypeAction
	{
		EUMGPrototypeNavigationAction Kind = EUMGPrototypeNavigationAction::Navigate;
		FString Payload;
		FUMGPrototypeTransitionData Transition;
		bool bResolved = false;
	};

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		return Design2UMGCore::SharedSemantics::HasRenderableVisual(Node);
	}

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		return Design2UMGCore::SharedSemantics::ShouldSkipNode(Node);
	}

	bool HasWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetString(Widget, Key);
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
			&& Node->Transform.Width.Value > 0.0f
			&& Node->Transform.Height.Value > 0.0f;
	}

	void GatherVisibleChildrenForConversion(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child) || ShouldKeepStructuralLeafInFlow(Child, Node->LayoutData.Layout))
			{
				OutVisibleChildren.Add(Child);
			}
		}
	}

	void WarmSharedTextNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context);

	struct FNodeConversionOrchestrator;

	struct FNodeChildAppender
	{
		FNodeChildAppender(
			FUMGWidgetNode& InWidget,
			const TSharedPtr<FDesignNode>& InNode,
			const TArray<TSharedPtr<FDesignNode>>& InVisibleChildren,
			const FConversionContext& InContext,
			const Design2UMGConverterInternal::FExpandedNodeResolver& InExpandedNodeResolver,
			const FNodeConversionOrchestrator& InOrchestrator)
			: Widget(InWidget)
			, Node(InNode)
			, VisibleChildren(InVisibleChildren)
			, Context(InContext)
			, ExpandedNodeResolver(InExpandedNodeResolver)
			, Orchestrator(InOrchestrator)
		{
		}

		void AppendChildren() const;

	private:
		bool ShouldWrapChildWithSizeBox(
			const TSharedPtr<FDesignNode>& ChildNode,
			const FUMGWidgetNode& ChildWidget) const
		{
			const bool bHasFixedSize = ChildNode->Transform.Width.IsFixed() || ChildNode->Transform.Height.IsFixed();
			const bool bHasMinSize = (ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Width.Value > 0)
				|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Height.Value > 0);
			const bool bCanvasMixedAxis = Node->LayoutData.Layout == EDesignLayout::None
				&& ((ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Height.IsFixed())
					|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Width.IsFixed()));
			const bool bWrapOrGridMeasuredFill =
				(Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
				&& ((ChildNode->Transform.Width.IsFill() && ChildNode->Transform.Width.Value > 0.0f)
					|| (ChildNode->Transform.Height.IsFill() && ChildNode->Transform.Height.Value > 0.0f));
			const bool bWidgetOwnsDesiredSize =
				Node->LayoutData.Layout != EDesignLayout::None
				&& ChildWidget.Type == TEXT("Image")
				&& ChildNode->Children.Num() == 0
				&& (HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::WidthOverride)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::HeightOverride))
				&& (HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::MaterialRef)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::TexturePath)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::PathGeometry));
			return Node->LayoutData.Layout == EDesignLayout::None
				? bCanvasMixedAxis
				: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
		}

		FUMGWidgetNode& Widget;
		const TSharedPtr<FDesignNode>& Node;
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren;
		const FConversionContext& Context;
		const Design2UMGConverterInternal::FExpandedNodeResolver& ExpandedNodeResolver;
		const FNodeConversionOrchestrator& Orchestrator;
	};

	struct FNodeConversionOrchestrator
	{
		explicit FNodeConversionOrchestrator(const FConversionContext& InRootContext)
			: RootContext(InRootContext)
		{
		}

		FUMGWidgetNode Convert(const TSharedPtr<FDesignNode>& Node, const EDesignLayout ParentLayout = EDesignLayout::None) const
		{
			return ConvertNode(Node, RootContext, ParentLayout);
		}

		FUMGWidgetNode ConvertChild(
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			return ConvertNode(Node, Context, ParentLayout);
		}

	private:
		FUMGWidgetNode ConvertNode(
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			FRecursionGuard Guard(Context.State.RecursionDepth, TEXT("ConvertNode"));
			if (!Guard.bValid || !ShouldConvertNode(Node, ParentLayout))
			{
				return FUMGWidgetNode();
			}

			const FConversionContext ActiveContext = Context.WithNodeTheme(Node);
			const Design2UMGConverterInternal::FExpandedNodeResolver ExpandedNodeResolver = BuildExpandedNodeResolver(ActiveContext);

			if (Node->Type == EDesignNodeType::Ref)
			{
				return ConvertExpandedRefNode(Node, ActiveContext, ParentLayout, ExpandedNodeResolver);
			}

			const bool bUsingRenderFallback = Design2UMGSourceMetadata::ShouldUseRenderFallback(Node);
			FUMGWidgetNode Widget = CreateBaseWidgetForNode(Node, Context.State);
			LogCreatedWidget(Widget, Node);

			if (!bUsingRenderFallback)
			{
				ApplyNodeVisuals(Widget, Node);
				AppendChildWidgets(Widget, Node, ActiveContext, ExpandedNodeResolver);
				ApplyLayoutFixup(Widget, Node, ActiveContext, ParentLayout);
				Widget = WrapWithPrototypeNavigation(MoveTemp(Widget), Node, ActiveContext);
			}
			else
			{
				ApplyRenderFallbackVisuals(Widget, Node);
			}
			Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);

			return Widget;
		}

		bool ShouldConvertNode(const TSharedPtr<FDesignNode>& Node, const EDesignLayout ParentLayout) const
		{
			return !ShouldSkipNode(Node) || ShouldKeepStructuralLeafInFlow(Node, ParentLayout);
		}

		Design2UMGConverterInternal::FExpandedNodeResolver BuildExpandedNodeResolver(
			const FConversionContext& Context) const
		{
			return Design2UMGConverterInternal::FExpandedNodeResolver(Context.State.Document, Context.ThemeContext);
		}

		FUMGWidgetNode ConvertExpandedRefNode(
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& ActiveContext,
			const EDesignLayout ParentLayout,
			const Design2UMGConverterInternal::FExpandedNodeResolver& ExpandedNodeResolver) const
		{
			TSharedPtr<FDesignNode> Expanded = ExpandedNodeResolver.ExpandRef(Node);
			if (!Expanded.IsValid())
			{
				return FUMGWidgetNode();
			}

			SizeEstimator::PreComputeFitContentSizes(Expanded, ActiveContext.State.Document);
			return ConvertNode(Expanded, ActiveContext, ParentLayout);
		}

		void LogCreatedWidget(const FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node) const
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
				*Widget.Name, *Widget.Id, *Widget.Type,
				Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
				(int32)Node->LayoutData.Layout, Node->Children.Num());
		}

		void ApplyNodeVisuals(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node) const
		{
			VisualSupport::ApplyBaseVisualProperties(Widget, Node);
		}

		void AppendChildWidgets(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const Design2UMGConverterInternal::FExpandedNodeResolver& ExpandedNodeResolver) const
		{
			TArray<TSharedPtr<FDesignNode>> VisibleChildren;
			GatherVisibleChildrenForConversion(Node, VisibleChildren);
			if (Node->LayoutData.Layout == EDesignLayout::Grid && Design2UMGGridLayout::NeedsGridPanel(VisibleChildren))
			{
				Widget.Type = TEXT("GridPanel");
			}

			const Design2UMGConverterInternal::FFlowChildPreparation FlowChildPreparation(ExpandedNodeResolver);
			FlowChildPreparation.PreResolveFlowChildren(Node, Widget, VisibleChildren);
			FNodeChildAppender(Widget, Node, VisibleChildren, Context, ExpandedNodeResolver, *this).AppendChildren();
		}

		void ApplyRenderFallbackVisuals(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node) const
		{
			VisualSupport::ApplyRenderFallbackVisuals(Widget, Node);
		}

		void ApplyLayoutFixup(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			const bool bHasResolvedGridGap = Node->LayoutData.Layout == EDesignLayout::Grid
				&& (Design2UMGGridLayout::ResolveGridColumnGap(Node) > KINDA_SMALL_NUMBER
					|| Design2UMGGridLayout::ResolveGridRowGap(Node) > KINDA_SMALL_NUMBER);
			if (Node->LayoutData.ExplicitGap.Get(0.0f) > 0 || bHasResolvedGridGap)
			{
				if (Node->LayoutData.Layout == EDesignLayout::Wrap)
				{
					const float CrossAxisGap = Design2UMGSourceMetadata::ResolveWrapCrossAxisGap(Node);
					Widget.WidgetStyle.InnerSlotPadding = FVector2D(Node->LayoutData.ExplicitGap.Get(0.0f), CrossAxisGap);
				}
				else if (Node->LayoutData.Layout == EDesignLayout::Grid)
				{
					if (Widget.Type == TEXT("UniformGridPanel"))
					{
						const float ColumnGap = Design2UMGGridLayout::ResolveGridColumnGap(Node);
						const float RowGap = Design2UMGGridLayout::ResolveGridRowGap(Node);
						if (ColumnGap > KINDA_SMALL_NUMBER || RowGap > KINDA_SMALL_NUMBER)
						{
							Widget.WidgetStyle.SlotPadding =
								FMargin(ColumnGap * 0.5f, RowGap * 0.5f, ColumnGap * 0.5f, RowGap * 0.5f);
						}
						else
						{
							Widget.WidgetStyle.SlotPadding.Reset();
						}
					}
				}
				else
				{
					LayoutMapper::ApplyGap(Widget.Children, Node->LayoutData.ExplicitGap.Get(0.0f), Node->LayoutData.Layout);
				}

				if (Node->LayoutData.Layout == EDesignLayout::Grid
					&& Widget.Type == TEXT("UniformGridPanel")
					&& !Widget.WidgetStyle.SlotPadding.IsSet())
				{
					Widget.WidgetStyle.SlotPadding = FMargin(Node->LayoutData.ExplicitGap.Get(0.0f), Node->LayoutData.ExplicitGap.Get(0.0f), Node->LayoutData.ExplicitGap.Get(0.0f), Node->LayoutData.ExplicitGap.Get(0.0f));
				}
			}

			if (Node->LayoutData.JustifyContent != EDesignJustifyContent::Start)
			{
				const EDesignSizeMode MainAxisMode = (Node->LayoutData.Layout == EDesignLayout::Horizontal)
					? Node->Transform.Width.Mode : Node->Transform.Height.Mode;
				LayoutMapper::ApplyJustifyContent(Widget.Children, Node->LayoutData.JustifyContent, MainAxisMode, ParentLayout);
			}

			VisualSupport::FConversionVisualSettingsPayload VisualSettings;
			VisualSettings.PlaceholderHatchStyle = Context.State.SettingsSnapshot.PlaceholderHatchStyle;
			VisualSettings.VisualMapperOptions = Context.State.SettingsSnapshot.VisualMapperOptions;

			const bool bPlaceholderUsesDirectImage = VisualSupport::TryApplyPlaceholderSurfaceTint(Widget, Node, VisualSettings);
			if (VisualSupport::ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
					*Widget.Name, VisualSupport::HasVisualFill(Node), Node->Visual.CornerRadius.HasRadius(), Node->Visual.Stroke.HasStroke());
				Widget = VisualSupport::WrapWithBorder(MoveTemp(Widget), Node);
			}
			else if (LayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
			{
				LayoutMapper::ApplyPadding(Widget, Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}));
			}

			Widget = VisualSupport::ApplyVisualEffectWrappers(MoveTemp(Widget), Node, VisualSettings.VisualMapperOptions);
		}

		FUMGWidgetNode WrapWithPrototypeNavigation(
			FUMGWidgetNode&& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context) const
		{
			if (Design2UMGPrototypeNavigation::ShouldWrapNodeWithButton(
				Node,
				Context.State.PrototypeNavigation,
				Context.State.PrototypeTriggerNodeIds))
			{
				return Design2UMGPrototypeNavigation::WrapWithButton(MoveTemp(Widget), Node);
			}
			return MoveTemp(Widget);
		}

		FUMGWidgetNode CreateBaseWidgetForNode(const TSharedPtr<FDesignNode>& Node, FConversionState& State) const
		{
			if (Design2UMGSourceMetadata::ShouldUseRenderFallback(Node))
			{
				FUMGWidgetNode Widget = Design2UMGSourceMetadata::CreateRenderFallbackWidget(Node);
				Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(Widget, Node);
				Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);
				UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' -> Figma render fallback image"), *Widget.Name);
				return Widget;
			}

			Design2UMGNodeWidgetFactory::FBuildArgs BuildArgs;
			BuildArgs.ExistingWarnedFamilies = &State.WarnedFamilies;
			BuildArgs.IconFontSettingsSnapshot = &State.SettingsSnapshot.IconFont;

			Design2UMGNodeWidgetFactory::FBuildResult BuildResult =
				Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(Node, BuildArgs);
			State.Warnings.Append(BuildResult.Warnings);
			for (const FString& WarnedFamily : BuildResult.NewlyWarnedFamilies)
			{
				State.WarnedFamilies.Add(WarnedFamily);
			}
			for (const FString& PrunedFamily : BuildResult.PrunedInvalidMappings)
			{
				State.PrunedInvalidMappings.Add(PrunedFamily);
			}
			for (const TPair<FString, FString>& PendingMapping : BuildResult.PendingIconFontMappings)
			{
				State.PendingIconFontMappings.Add(PendingMapping.Key, PendingMapping.Value);
			}

			FUMGWidgetNode Widget = MoveTemp(BuildResult.Widget);
			Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(Widget, Node);
			return Widget;
		}

		const FConversionContext& RootContext;
	};

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
			const Design2UMGConverterInternal::FExpandedNodeResolver ExpandedNodeResolver(
				ActiveContext.State.Document,
				ActiveContext.ThemeContext);
			const TSharedPtr<FDesignNode> ActiveNode = ExpandedNodeResolver.ExpandRef(Node);
			if (!ActiveNode.IsValid() || ShouldSkipNode(ActiveNode))
			{
				return;
			}

			WarmSharedTextNode(ActiveNode, ActiveContext.WithNodeTheme(ActiveNode));
			return;
		}

		WarmSharedTextNode(Node, ActiveContext);
	}

	void WarmSharedTextNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || ShouldSkipNode(Node))
		{
			return;
		}

		if (Node->Type == EDesignNodeType::Text
			&& Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top
			&& !Node->Text.Content.Contains(TEXT("\n"))
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

	void FNodeChildAppender::AppendChildren() const
	{
		const bool bGridPanelParent =
			Node.IsValid()
			&& Node->LayoutData.Layout == EDesignLayout::Grid
			&& Widget.Type.Equals(TEXT("GridPanel"), ESearchCase::CaseSensitive);
		Design2UMGGridLayout::FGridPlacementState GridPlacementState;
		if (bGridPanelParent)
		{
			GridPlacementState.ColumnCount = Design2UMGGridLayout::ResolveGridColumnCount(Node, VisibleChildren);
			GridPlacementState.RowCount = Design2UMGGridLayout::ResolveGridRowCount(Node, VisibleChildren, GridPlacementState.ColumnCount);
		}

		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			TSharedPtr<FDesignNode> EffectiveChild = ExpandedNodeResolver.ResolveExpandedChild(Child, true);
			FUMGWidgetNode ChildWidget = Orchestrator.ConvertChild(EffectiveChild, Context, Node->LayoutData.Layout);
			if (ChildWidget.Type.IsEmpty())
			{
				continue;
			}

			LayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->LayoutData.Layout);
			LayoutMapper::ApplyAlignItems(ChildWidget, Node->LayoutData.AlignItems, Node->LayoutData.Layout);
			if (Node->LayoutData.Layout == EDesignLayout::Grid)
			{
				if (bGridPanelParent)
				{
					Design2UMGGridLayout::ApplyGridPanelChildSlotData(ChildWidget, Node, EffectiveChild, GridPlacementState);
				}
				else
				{
					if (EffectiveChild->LayoutData.HasLayoutRowValue())
					{
						ChildWidget.Slot.Row = FMath::Max(0, EffectiveChild->LayoutData.GetLayoutRowOr(0));
					}
					if (EffectiveChild->LayoutData.HasLayoutColumnValue())
					{
						ChildWidget.Slot.Column = FMath::Max(0, EffectiveChild->LayoutData.GetLayoutColumnOr(0));
					}
					Design2UMGGridLayout::ApplyGridChildAlignmentOverrides(ChildWidget, Node, EffectiveChild);
				}
			}
			if (Node->LayoutData.Layout != EDesignLayout::None && !EffectiveChild->LayoutData.bAbsoluteLayoutPositioning)
			{
				ChildWidget.Bounds.X = 0.0f;
				ChildWidget.Bounds.Y = 0.0f;
			}

			if (ShouldWrapChildWithSizeBox(EffectiveChild, ChildWidget))
			{
				const bool bHasFixedSize = EffectiveChild->Transform.Width.IsFixed() || EffectiveChild->Transform.Height.IsFixed();
				const bool bHasMinSize = (EffectiveChild->Transform.Width.IsFit() && EffectiveChild->Transform.Width.Value > 0)
					|| (EffectiveChild->Transform.Height.IsFit() && EffectiveChild->Transform.Height.Value > 0);
				UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
					*ChildWidget.Name, bHasFixedSize, bHasMinSize,
					EffectiveChild->Transform.Width.Value, EffectiveChild->Transform.Height.Value);
				Widget.Children.Add(LayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild, Node->LayoutData.Layout));
				continue;
			}

			Widget.Children.Add(MoveTemp(ChildWidget));
		}
	}
}

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;

	for (const FDesignSkippedNodeReport& Report : Document.ParseDiagnostics().SkippedDesignerNodes)
	{
		Result.Warnings.AddUnique(FString::Printf(
			TEXT("Figma %s '%s' (%s) skipped from runtime widget tree [%s]"),
			*Report.NodeType,
			Report.NodeName.IsEmpty() ? TEXT("<unnamed>") : *Report.NodeName,
			Report.NodeId.IsEmpty() ? TEXT("<no-id>") : *Report.NodeId,
			Report.Reason.IsEmpty() ? TEXT("designer_only") : *Report.Reason));
	}

	const FConversionSettingsSnapshot SettingsSnapshot = BuildConversionSettingsSnapshot();
	FConversionState State(Document, Result.Warnings, SettingsSnapshot);
	FConversionContext RootContext(State);
	const FNodeConversionOrchestrator NodeOrchestrator(RootContext);

	if (!Document.SourceDocument().ComponentsJson.IsEmpty() || !Document.SourceDocument().ComponentSetsJson.IsEmpty())
	{
		FUMGSourceDocumentData& SourceDocument = Result.MutableSourceDocument();
		SourceDocument.ComponentsJson = Document.SourceDocument().ComponentsJson;
		SourceDocument.ComponentSetsJson = Document.SourceDocument().ComponentSetsJson;
	}

	TextMeasurement::ResetCache();

	// Deep-copy children so conversion never mutates the caller's source tree through shared refs.
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(RefExpander::DeepCopyNode(Child));

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
		const Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis PrototypeAnalysis =
			Design2UMGPrototypeNavigation::BuildAnalysis(Document, VisibleChildren, Result.Warnings);
		State.PrototypeNavigation = PrototypeAnalysis.Data;
		State.PrototypeTriggerNodeIds = PrototypeAnalysis.TriggerNodeIds;
		if (PrototypeAnalysis.Data.IsValid())
		{
			Result.MutablePrototypeNavigation() = PrototypeAnalysis.Data;
		}
	}

	// Use the first non-skipped frame name for WidgetName, then fall back to WBP_FromPen.
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

	// Pre-compute fit-content node sizes by recursively resolving child-driven bounds.
	for (const auto& Child : VisibleChildren)
	{
		SizeEstimator::PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable)
			continue;

		Child->Transform.X = 0.0f;
		Child->Transform.Y = 0.0f;

		FUMGWidgetNode ChildWidget = NodeOrchestrator.Convert(Child);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (State.PrototypeNavigation.IsValid())
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Convert: %d top-level widgets → prototype root with %d navigation bindings"),
			RenderWidgets.Num(),
			State.PrototypeNavigation.ClickBindings.Num());
		Result.RootWidget = Design2UMGPrototypeNavigation::BuildRoot(MoveTemp(RenderWidgets), State.PrototypeNavigation);
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

