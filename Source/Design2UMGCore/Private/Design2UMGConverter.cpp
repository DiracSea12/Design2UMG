// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignComponentMapping.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGCanvasFlattening.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "Design2UMGGridLayout.h"
#include "Design2UMGNodeWidgetFactory.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGConverterFlowChildPreResolution.h"
#include "Design2UMGConverterInternal.h"
#include "Design2UMGConverterVisualSupport.h"
#include "Design2UMGSourceMetadata.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignVariableResolver.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "UMGWidgetTypes.h"
#include "SizeEstimator.h"
#include "DesignSourceExportBounds.h"
#include "DesignNodeSemantics.h"
#include "TextMeasurement.h"
#include "RefExpander.h"
#include "PencilScript.h"
#include "Misc/EngineVersionComparison.h"

namespace Design2UMGConverterInternal
{
FUMGWidgetNode CreateFallbackBranchWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget = Design2UMGConverterVisualSupport::CreateRenderFallbackWidget(Node);
	Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(Widget, Node);
	Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);
	UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' -> render fallback image"), *Widget.Name);
	return Widget;
}
} // namespace Design2UMGConverterInternal

namespace Design2UMGConverterPrivate
{
	using namespace Design2UMGConverterInternal;
	namespace VisualSupport = Design2UMGConverterVisualSupport;

	void AppendUnknownNodeWarnings(const TSharedPtr<FDesignNode>& Node, TArray<FString>& Warnings)
	{
		if (!Node.IsValid()) return;
		if (Node->Type == EDesignNodeType::Unknown && !Node->Source().SourceNodeType.IsEmpty())
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Node '%s' (%s) has unsupported source type '%s'."),
				*Node->Name, *Node->Id, *Node->Source().SourceNodeType));
		}
		for (const auto& Child : Node->Children)
		{
			AppendUnknownNodeWarnings(Child, Warnings);
		}
	}

	FConversionSettingsSnapshot BuildConversionSettingsSnapshot(const FDesign2UMGConversionSettings& Settings)
	{
		FConversionSettingsSnapshot Snapshot;
		Snapshot.FontMappings = Settings.ExplicitFontMappings;
		Snapshot.PlaceholderHatchStyle = Settings.PlaceholderHatchStyle;
		Snapshot.VisualMapperOptions.LuminosityFitOpacityCap = Settings.LuminosityFitOpacityCap;
		Snapshot.IconFont.IconFontAssetPaths = Settings.IconFontAssetPaths;
		Snapshot.IconFont.DefaultIconFontAssetPath = Settings.DefaultIconFontAssetPath;
		Snapshot.IconFont.MissingIconFontPlaceholder = Settings.MissingIconFontPlaceholder;
		return Snapshot;
	}

	void MarkMappedComponentCandidatesRecursive(FUMGWidgetNode& Node, const FString& DocumentScope)
	{
		DesignComponentMapping::FDesignComponentIdentity Identity;
		if (Node.HasSourceData()
			&& DesignComponentMapping::TryResolveFigmaComponentIdentity(Node.Source(), DocumentScope, Identity))
		{
			Node.MutableSource().bMappedComponentCandidate = true;
		}
		for (FUMGWidgetNode& Child : Node.Children)
		{
			MarkMappedComponentCandidatesRecursive(Child, DocumentScope);
		}
	}

	// At conversion finalization, stamp the document-level provider and DocumentScope into the IR document sidecar, and mark nodes
	// with resolvable Figma component identities as mapped candidates. Matching decisions are not made here (Editor resolves them at generation time).
	void StampDocumentComponentIdentity(const FDesignDocument& Document, FUMGWidgetData& Result)
	{
		const FString Provider = Document.Metadata.FindRef(TEXT("source.provider"));
		if (Provider.IsEmpty())
		{
			return;
		}

		const bool bFigma = Provider.Equals(DesignComponentMapping::ProviderFigma, ESearchCase::IgnoreCase);
		const bool bPencil = Provider.Equals(DesignComponentMapping::ProviderPencil, ESearchCase::IgnoreCase);
		if (!bFigma && !bPencil)
		{
			return;
		}

		const FString FileKey = bFigma ? Document.Metadata.FindRef(TEXT("fileKey")) : FString();
		FString ImportSource = Document.SourceDocument().SourceFilePath;
		if (ImportSource.IsEmpty())
		{
			ImportSource = Document.Metadata.FindRef(TEXT("design.sourceFilePath"));
		}
		const DesignComponentMapping::FDocumentScopeResolution Scope =
			DesignComponentMapping::ResolveFigmaDocumentScope(FileKey, ImportSource);

		FUMGSourceDocumentData& SourceDocument = Result.MutableSourceDocument();
		SourceDocument.Provider = bFigma ? DesignComponentMapping::ProviderFigma : DesignComponentMapping::ProviderPencil;
		SourceDocument.DocumentScope = Scope.Value;
		SourceDocument.DocumentScopeOrigin = Scope.Source;

		if (bFigma && Scope.IsResolved())
		{
			MarkMappedComponentCandidatesRecursive(Result.RootWidget, Scope.Value);
		}
	}

	bool ShouldKeepStructuralLeafInFlow(const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
	{
		if (!Node.IsValid()
			|| (ParentLayout != EDesignLayout::Wrap && ParentLayout != EDesignLayout::Grid))
		{
			return false;
		}

		if (Node->Children.Num() > 0 || Design2UMGCore::DesignNodeSemantics::HasRenderableVisual(Node))
		{
			return false;
		}

		// Empty structural leaves can still reserve flow/grid cells, so keep their measured footprint.
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
			if (!Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(Child) || ShouldKeepStructuralLeafInFlow(Child, Node->LayoutData.Layout))
			{
				OutVisibleChildren.Add(Child);
			}
		}
	}

	struct FDeferredExportSubtree
	{
		TSharedPtr<FDesignNode> Parent;
		int32 ChildIndex;
		// Cache keys may still identify the original ref during later bounds queries.
		TSharedPtr<FDesignNode> OriginalChild;
		TSharedPtr<FDesignNode> ConvertedChild;
	};

	struct FNodeConversionOrchestrator
	{
		FNodeConversionOrchestrator(const FConversionContext& InRootContext,
			const FDesign2UMGConverter::FPrepareFonts& InPrepareFonts,
			TArray<FDeferredExportSubtree>* InExportSubtrees)
			: RootContext(InRootContext)
			, PrepareFonts(InPrepareFonts)
			, ExportSubtrees(InExportSubtrees)
		{
		}

		FUMGWidgetNode Convert(TSharedPtr<FDesignNode>& Node, const EDesignLayout ParentLayout = EDesignLayout::None) const
		{
			return ConvertNode(Node, RootContext, ParentLayout);
		}

		FUMGWidgetNode ConvertChild(
			TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			return ConvertNode(Node, Context, ParentLayout);
		}

	private:
		FUMGWidgetNode ConvertNode(
			TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			if (!Context.State.ConversionError.IsEmpty())
			{
				return FUMGWidgetNode();
			}

			FRecursionGuard Guard(Context.State.RecursionDepth, TEXT("ConvertNode"));
			if (!Guard.bValid)
			{
				Context.State.ConversionError = FString::Printf(
					TEXT("Design conversion failed at node '%s': expanded design tree exceeds %d levels."),
					Node.IsValid() ? *Node->Id : TEXT("<invalid>"),
					DesignTreeLimits::MaxNodeDepth);
				return FUMGWidgetNode();
			}
			if (!ShouldConvertNode(Node, ParentLayout))
			{
				return FUMGWidgetNode();
			}

			const FConversionContext ActiveContext = Context.WithNodeTheme(Node);
			for (const FString& Warning : Node->Text.ParseWarnings)
			{
				Context.State.Warnings.AddUnique(FString::Printf(
					TEXT("Text node '%s' (%s): %s"), *Node->Name, *Node->Id, *Warning));
			}
			const Design2UMGConverterInternal::FFlowChildPreResolutionResolver PreResolutionResolver = BuildPreResolutionResolver(ActiveContext);

			// Refs are expanded before visual/layout work so children inherit the active theme and measured sizes together.
			if (Node->Type == EDesignNodeType::Ref)
			{
				return ConvertExpandedRefNode(Node, ActiveContext, ParentLayout, PreResolutionResolver);
			}
			if (Node->Type == EDesignNodeType::Script)
			{
				FDesignVariableResolver Resolver(Context.State.Document.Variables);
				Resolver.SetThemeContext(FDesignVariableResolver::BuildDocumentTheme(Context.State.Document.Themes));
				Resolver.PushThemeOverride(ActiveContext.ThemeContext);
				TSharedPtr<FDesignNode> Expanded;
				FString Error;
				if (!PencilScript::Expand(Node, Resolver, ActiveContext.InstancePath,
					Context.State.Document.SourceDocument().SourceFilePath, Expanded, Error))
				{
					Context.State.ConversionError = FString::Printf(TEXT("Script '%s' (%s): %s"), *Node->Name, *Node->Id, *Error);
					return FUMGWidgetNode();
				}
				Node = MoveTemp(Expanded);
				const auto GeneratedSnapshot = RefExpander::DeepCopyNode(Node);
				TArray<TSharedPtr<FDesignNode>> Pending = GeneratedSnapshot->Children;
				TArray<TSharedPtr<FDesignNode>> GeneratedRefs;
				while (!Pending.IsEmpty())
				{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
					auto Generated = Pending.Pop(false);
#else
					auto Generated = Pending.Pop(EAllowShrinking::No);
#endif
					if (++Context.State.ScriptGeneratedNodeCount > DesignTreeLimits::MaxParsedNodeCount)
					{
						Context.State.ConversionError = TEXT("Expanded scripts exceed the design tree node budget.");
						return FUMGWidgetNode();
					}
					Context.State.RefExpansionCache->ReferenceTargetsById.Add(Generated->Id, Generated);
					if (Generated->Type == EDesignNodeType::Ref) GeneratedRefs.Add(Generated);
					Pending.Append(Generated->Children);
				}
				for (const auto& Ref : GeneratedRefs)
				{
					if (!Context.State.RefExpansionCache->ReferenceTargetsById.Contains(Ref->Ref))
					{
						Context.State.ConversionError = FString::Printf(TEXT("Script '%s' generated unresolved ref '%s'."), *Node->Id, *Ref->Ref);
						return FUMGWidgetNode();
					}
				}
				if (PrepareFonts)
				{
					// Earlier parent sizing sees only the unexpanded script's resolved box.
					// Its generated text first becomes measurable below this preparation point.
					FDesignDocument GeneratedDocument;
					GeneratedDocument.Fonts = Context.State.Document.Fonts;
					GeneratedDocument.Children.Add(Node);
					if (!PrepareFonts(GeneratedDocument, Context.State.Warnings, Context.State.ConversionError,
						FDesign2UMGConverter::EFontPreparation::ImportSources))
					{
						return FUMGWidgetNode();
					}
				}
			}

			const bool bUsingRenderFallback = Design2UMGConverterVisualSupport::ShouldUseRenderFallback(Node);
			FUMGWidgetNode Widget = CreateBaseWidgetForNode(Node, Context.State, bUsingRenderFallback);
			LogCreatedWidget(Widget, Node);

			if (!bUsingRenderFallback)
			{
				VisualSupport::ApplyBaseVisualProperties(Widget, Node);
				TArray<FUMGWidgetNode> AbsoluteChildren;
				AppendChildWidgets(Widget, Node, ActiveContext, PreResolutionResolver, AbsoluteChildren);
				ApplyLayoutAdjustments(Widget, Node, ActiveContext, ParentLayout, MoveTemp(AbsoluteChildren));
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
			return !Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(Node) || ShouldKeepStructuralLeafInFlow(Node, ParentLayout);
		}

		Design2UMGConverterInternal::FFlowChildPreResolutionResolver BuildPreResolutionResolver(
			const FConversionContext& Context) const
		{
			return Design2UMGConverterInternal::FFlowChildPreResolutionResolver(
				Context.State.Document,
				Context.ThemeContext,
				Context.State.RefExpansionCache,
				&Context.State.SettingsSnapshot.FontMappings, PrepareFonts);
		}

		FUMGWidgetNode ConvertExpandedRefNode(
			TSharedPtr<FDesignNode>& Node,
			const FConversionContext& ActiveContext,
			const EDesignLayout ParentLayout,
			const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver) const
		{
			TSharedPtr<FDesignNode> Expanded = PreResolutionResolver.MakeWorkingCopy(Node);
			if (!Expanded.IsValid())
			{
				return FUMGWidgetNode();
			}

			const auto RefResolver =
				[&PreResolutionResolver](const TSharedPtr<FDesignNode>& RefNode)
				{
					return PreResolutionResolver.MakeWorkingCopy(RefNode);
				};
			SizeEstimator::PromoteFitContentSizes(
				Expanded,
				ActiveContext.State.Document,
				RefResolver,
				&ActiveContext.State.SettingsSnapshot.FontMappings,
				&ActiveContext.State.ConversionError);
			Node = MoveTemp(Expanded);
			return ConvertNode(Node, ActiveContext, ParentLayout);
		}

		void LogCreatedWidget(const FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node) const
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
				*Widget.Name, *Widget.Id, *Widget.Type,
				Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
				(int32)Node->LayoutData.Layout, Node->Children.Num());
		}

		void AppendChildWidgets(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver,
			TArray<FUMGWidgetNode>& OutAbsoluteChildren) const
		{
			TArray<TSharedPtr<FDesignNode>> VisibleChildren;
			GatherVisibleChildrenForConversion(Node, VisibleChildren);
			if (Node->LayoutData.Layout != EDesignLayout::None)
			{
				TArray<TSharedPtr<FDesignNode>> FlowChildren;
				int32 VisibleIndex = 0;
				for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num() && VisibleIndex < VisibleChildren.Num(); ++ChildIndex)
				{
					if (Node->Children[ChildIndex] != VisibleChildren[VisibleIndex]) continue;
					const TSharedPtr<FDesignNode> Child = VisibleChildren[VisibleIndex++];
					if (!Child->LayoutData.bAbsoluteLayoutPositioning)
					{
						FlowChildren.Add(Child);
						continue;
					}
					TSharedPtr<FDesignNode> EffectiveChild = PreResolutionResolver.ResolveExpandedChild(Child, true);
					FUMGWidgetNode AbsoluteWidget = ConvertChild(EffectiveChild, Context, EDesignLayout::None);
					if (ExportSubtrees && Child != EffectiveChild)
						ExportSubtrees->Add({Node, ChildIndex, Child, EffectiveChild});
					if (!AbsoluteWidget.Type.IsEmpty())
					{
						LayoutMapper::ApplySlotProperties(AbsoluteWidget, EffectiveChild, EDesignLayout::None);
						OutAbsoluteChildren.Add(MoveTemp(AbsoluteWidget));
					}
				}
				VisibleChildren = MoveTemp(FlowChildren);
			}
			// Grid selection and flow prep must see the same filtered child list to avoid layout drift.
			if (Node->LayoutData.Layout == EDesignLayout::Grid && Design2UMGGridLayout::NeedsGridPanel(VisibleChildren))
			{
				Widget.Type = UMGWidgetTypes::GridPanel;
			}

			// Flow children are pre-resolved before appending so slot metrics are based on effective nodes, not raw refs.
			PreResolutionResolver.PreResolveFlowChildren(Node, Widget, VisibleChildren);
			AppendChildren(Widget, Node, VisibleChildren, Context, PreResolutionResolver);
		}

		bool ShouldWrapChildWithSizeBox(
			const TSharedPtr<FDesignNode>& Node,
			const TSharedPtr<FDesignNode>& ChildNode,
			const FUMGWidgetNode& ChildWidget) const
		{
			const bool bHasFixedSize = ChildNode->Transform.Width.IsFixed() || ChildNode->Transform.Height.IsFixed();
			const bool bHasMinSize = (ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Width.Value > 0)
				|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Height.Value > 0);
			// SizeBox carries source-side measured constraints that panel slots cannot express on their own.
			const bool bCanvasMixedAxis = Node->LayoutData.Layout == EDesignLayout::None
				&& ChildWidget.Slot.bAutoSize
				&& ((ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Height.IsFixed())
					|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Width.IsFixed()));
			const bool bWrapOrGridMeasuredFill =
				(Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
				&& ((ChildNode->Transform.Width.IsFill() && ChildNode->Transform.Width.Value > 0.0f)
					|| (ChildNode->Transform.Height.IsFill() && ChildNode->Transform.Height.Value > 0.0f));
			// Direct typed reads with descriptor-identical presence rules:
			// Width/HeightOverride and PathGeometry are plain optionals, while
			// MaterialRef presence also covers a gradient-owned brush and
			// TexturePath lives on the canonical image owner.
			const bool bWidgetOwnsDesiredSize =
				Node->LayoutData.Layout != EDesignLayout::None
				&& ChildWidget.Type == UMGWidgetTypes::Image
				&& ChildNode->Children.Num() == 0
				&& (ChildWidget.WidgetStyle.WidthOverride.IsSet()
					|| ChildWidget.WidgetStyle.HeightOverride.IsSet())
				&& ((ChildWidget.Brush.Gradient.HasAny() || ChildWidget.Brush.MaterialRef.IsSet())
					|| ChildWidget.Brush.Image.TexturePath.IsSet()
					|| ChildWidget.Path.Geometry.IsSet());
			// Images with their own brush size already produce the intended desired size; wrapping would double-apply it.
			return Node->LayoutData.Layout == EDesignLayout::None
				? bCanvasMixedAxis
				: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
		}

		void AppendChildren(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
			const FConversionContext& Context,
			const Design2UMGConverterInternal::FFlowChildPreResolutionResolver& PreResolutionResolver) const
		{
			const bool bGridPanelParent =
				Node.IsValid()
				&& Node->LayoutData.Layout == EDesignLayout::Grid
				&& Widget.Type.Equals(UMGWidgetTypes::GridPanel, ESearchCase::CaseSensitive);
			Design2UMGGridLayout::FGridPlacementState GridPlacementState;
			if (bGridPanelParent)
			{
				GridPlacementState.ColumnCount = Design2UMGGridLayout::ResolveGridColumnCount(Node, VisibleChildren);
				GridPlacementState.RowCount = Design2UMGGridLayout::ResolveGridRowCount(Node, VisibleChildren, GridPlacementState.ColumnCount);
			}

			int32 VisibleIndex = 0;
			for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num() && VisibleIndex < VisibleChildren.Num(); ++ChildIndex)
			{
				if (Node->Children[ChildIndex] != VisibleChildren[VisibleIndex]) continue;
				const TSharedPtr<FDesignNode> Child = VisibleChildren[VisibleIndex++];
				TSharedPtr<FDesignNode> EffectiveChild = PreResolutionResolver.ResolveExpandedChild(Child, true);
				FUMGWidgetNode ChildWidget = ConvertChild(EffectiveChild, Context, Node->LayoutData.Layout);
				if (ExportSubtrees && Child != EffectiveChild)
					ExportSubtrees->Add({Node, ChildIndex, Child, EffectiveChild});
				if (ChildWidget.Type.IsEmpty())
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("Converter: skipped child '%s' (id='%s') with empty widget type"),
						*EffectiveChild->Name, *EffectiveChild->Id);
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
						if (EffectiveChild->LayoutData.LayoutRow.IsSet())
						{
							ChildWidget.Slot.Row = FMath::Max(0, EffectiveChild->LayoutData.LayoutRow.GetValue());
						}
						if (EffectiveChild->LayoutData.LayoutColumn.IsSet())
						{
							ChildWidget.Slot.Column = FMath::Max(0, EffectiveChild->LayoutData.LayoutColumn.GetValue());
						}
						Design2UMGGridLayout::ApplyGridChildAlignmentOverrides(ChildWidget, Node, EffectiveChild);
					}
				}
				if (Node->LayoutData.Layout != EDesignLayout::None && !EffectiveChild->LayoutData.bAbsoluteLayoutPositioning)
				{
					ChildWidget.Bounds.X = 0.0f;
					ChildWidget.Bounds.Y = 0.0f;
				}

				const bool bWrapChildWithSizeBox = ShouldWrapChildWithSizeBox(Node, EffectiveChild, ChildWidget);
				if (bWrapChildWithSizeBox)
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

		void ApplyRenderFallbackVisuals(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node) const
		{
			VisualSupport::ApplyRenderFallbackVisuals(Widget, Node);
		}

		void ApplyLayoutAdjustments(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout,
			TArray<FUMGWidgetNode> AbsoluteChildren) const
		{
			const bool bHasResolvedGridGap = Node->LayoutData.Layout == EDesignLayout::Grid
				&& (Design2UMGGridLayout::ResolveGridColumnGap(Node) > KINDA_SMALL_NUMBER
					|| Design2UMGGridLayout::ResolveGridRowGap(Node) > KINDA_SMALL_NUMBER);
			// Wrap writes dual-axis InnerSlotPadding once; it does not add per-child linear gaps.
			// A negative main-axis gap suppresses spacing. Zero allows a positive cross-axis gap.
			if (Node->LayoutData.Layout == EDesignLayout::Wrap)
			{
				const float MainAxisGap = Node->LayoutData.ExplicitGap.Get(0.0f);
				// The cross axis keeps its existing "fall back to the main axis" default
				// when counterAxisSpacing is absent.
				const float CrossAxisGap = Node->LayoutData.WrapCrossAxisGap.Get(MainAxisGap);
				// Use exact zero: even a small negative main-axis gap must suppress spacing.
				const bool bHasPositiveSpacing = MainAxisGap > 0.0f
					|| (MainAxisGap == 0.0f && CrossAxisGap > 0.0f);
				if (bHasPositiveSpacing)
				{
					Widget.WidgetStyle.InnerSlotPadding = FVector2D(MainAxisGap, CrossAxisGap);
				}
			}
			else if (Node->LayoutData.ExplicitGap.Get(0.0f) > 0 || bHasResolvedGridGap)
			{
				if (Node->LayoutData.Layout == EDesignLayout::Grid)
				{
					if (Widget.Type == UMGWidgetTypes::UniformGridPanel)
					{
						// Uniform grid slots carry half the resolved gap on each side.
						// With no resolved gaps the outer explicit gap becomes the
						// uniform slot padding (the outer condition guarantees it is
						// > 0 in that case, since bHasResolvedGridGap is false here).
						const float ColumnGap = Design2UMGGridLayout::ResolveGridColumnGap(Node);
						const float RowGap = Design2UMGGridLayout::ResolveGridRowGap(Node);
						if (ColumnGap > KINDA_SMALL_NUMBER || RowGap > KINDA_SMALL_NUMBER)
						{
							Widget.WidgetStyle.SlotPadding =
								FMargin(ColumnGap * 0.5f, RowGap * 0.5f, ColumnGap * 0.5f, RowGap * 0.5f);
						}
						else
						{
							const float UniformGap = Node->LayoutData.ExplicitGap.Get(0.0f);
							Widget.WidgetStyle.SlotPadding = FMargin(UniformGap, UniformGap, UniformGap, UniformGap);
						}
					}
				}
				else
				{
					LayoutMapper::ApplyGap(Widget.Children, Node->LayoutData.ExplicitGap.Get(0.0f), Node->LayoutData.Layout);
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

		FUMGWidgetNode CreateBaseWidgetForNode(const TSharedPtr<FDesignNode>& Node, FConversionState& State, const bool bUsingRenderFallback) const
		{
			if (bUsingRenderFallback)
			{
				return Design2UMGConverterInternal::CreateFallbackBranchWidget(Node);
			}

			Design2UMGNodeWidgetFactory::FBuildArgs BuildArgs;
			BuildArgs.FontMappings = &State.SettingsSnapshot.FontMappings;
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
		const FDesign2UMGConverter::FPrepareFonts& PrepareFonts;
		TArray<FDeferredExportSubtree>* ExportSubtrees;
	};
} // namespace Design2UMGConverterPrivate

FDesign2UMGConversionSettings FDesign2UMGConversionSettings::Defaults()
{
	FDesign2UMGConversionSettings Settings;
	Settings.PlaceholderHatchStyle = UDesign2UMGSettings::FResolvedPlaceholderHatchStyle();
	Settings.PlaceholderHatchStyle.BaseFillColor = UDesign2UMGSettings::CanonicalPlaceholderHatchBackgroundColor;
	Settings.PlaceholderHatchStyle.RefFillColor = UDesign2UMGSettings::CanonicalPlaceholderRefHatchBackgroundColor;
	Settings.PlaceholderHatchStyle.BaseStripeColor = UDesign2UMGSettings::CanonicalPlaceholderHatchStripeColor;
	Settings.PlaceholderHatchStyle.RefStripeColor = UDesign2UMGSettings::CanonicalPlaceholderRefHatchStripeColor;
	Settings.PlaceholderHatchStyle.StripeWidthPx = UDesign2UMGSettings::CanonicalPlaceholderHatchStripeWidth;
	Settings.PlaceholderHatchStyle.StripeGapPx = UDesign2UMGSettings::CanonicalPlaceholderHatchStripeGap;
	Settings.LuminosityFitOpacityCap = UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
	return Settings;
}

FDesign2UMGConversionSettings FDesign2UMGConversionSettings::FromSettings(const UDesign2UMGSettings* SourceSettings)
{
	FDesign2UMGConversionSettings Settings = Defaults();
	if (!SourceSettings)
	{
		return Settings;
	}

	Settings.ExplicitFontMappings = SourceSettings->ExplicitFontMappings;
	Settings.DefaultFontAssetPath = SourceSettings->DefaultFontAsset.ToSoftObjectPath().ToString();
	Settings.PlaceholderHatchStyle = SourceSettings->ResolvePlaceholderHatchStyle();
	Settings.LuminosityFitOpacityCap = SourceSettings->ResolveLuminosityFitOpacityCap();
	Settings.DefaultIconFontAssetPath = SourceSettings->DefaultIconFontAssetPath;
	Settings.MissingIconFontPlaceholder = SourceSettings->MissingIconFontPlaceholder;
	for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : SourceSettings->IconFontAssetMap)
	{
		const FSoftObjectPath AssetPath = Pair.Value.ToSoftObjectPath();
		if (!AssetPath.IsNull())
		{
			Settings.IconFontAssetPaths.Add(Pair.Key, AssetPath.ToString());
		}
	}
	return Settings;
}

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document, const FDesign2UMGConversionSettings& Settings,
	const FPrepareFonts& PrepareFonts)
{
	FUMGWidgetData Result;
	for (const auto& Child : Document.Children)
	{
		Design2UMGConverterPrivate::AppendUnknownNodeWarnings(Child, Result.Warnings);
	}

	for (const FDesignSkippedNodeReport& Report : Document.ParseDiagnostics().SkippedDesignerNodes)
	{
		Result.Warnings.AddUnique(FString::Printf(
			TEXT("Node %s '%s' (%s) skipped from runtime widget tree [%s]"),
			*Report.NodeType,
			Report.NodeName.IsEmpty() ? TEXT("<unnamed>") : *Report.NodeName,
			Report.NodeId.IsEmpty() ? TEXT("<no-id>") : *Report.NodeId,
			Report.Reason.IsEmpty() ? TEXT("designer_only") : *Report.Reason));
	}

	const Design2UMGConverterInternal::FConversionSettingsSnapshot SettingsSnapshot =
		Design2UMGConverterPrivate::BuildConversionSettingsSnapshot(Settings);
	Design2UMGConverterInternal::FConversionState State(Document, Result.Warnings, SettingsSnapshot);
	auto FinalizeResult = [&Result, &State, &Document]()
	{
		if (!State.ConversionError.IsEmpty())
		{
			Result.RootWidget = FUMGWidgetNode();
			Result.ConversionError = State.ConversionError;
			return;
		}

		Design2UMGCanvasFlattening::FFlattenStats FlattenStats;
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Result.RootWidget, FlattenStats);
		if (FlattenStats.MergedNestedCanvasCount > 0 || FlattenStats.RemovedEmptyCanvasCount > 0
			|| FlattenStats.RemovedRedundantSizeCarrierCount > 0)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("Convert: flattened %d nested bare canvas panels, removed %d empty canvas panels and %d redundant size carriers"),
				FlattenStats.MergedNestedCanvasCount, FlattenStats.RemovedEmptyCanvasCount, FlattenStats.RemovedRedundantSizeCarrierCount);
		}
		Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
		Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
		Design2UMGConverterPrivate::StampDocumentComponentIdentity(Document, Result);
	};
	Design2UMGConverterInternal::FConversionContext RootContext(State);
	TArray<Design2UMGConverterPrivate::FDeferredExportSubtree> ExportSubtrees;
	const Design2UMGConverterPrivate::FNodeConversionOrchestrator NodeOrchestrator(RootContext, PrepareFonts,
		Document.Metadata.FindRef(TEXT("source.provider")) == DesignComponentMapping::ProviderPencil ? &ExportSubtrees : nullptr);

	if (!Document.SourceDocument().ComponentsJson.IsEmpty() || !Document.SourceDocument().ComponentSetsJson.IsEmpty()
		|| !Document.SourceDocument().ComponentDependenciesJson.IsEmpty())
	{
		FUMGSourceDocumentData& SourceDocument = Result.MutableSourceDocument();
		SourceDocument.ComponentsJson = Document.SourceDocument().ComponentsJson;
		SourceDocument.ComponentSetsJson = Document.SourceDocument().ComponentSetsJson;
		SourceDocument.ComponentDependenciesJson = Document.SourceDocument().ComponentDependenciesJson;
		SourceDocument.ResolvedComponentImageUrls = Document.SourceDocument().ResolvedComponentImageUrls;
		SourceDocument.SourceFilePath = Document.SourceDocument().SourceFilePath;
	}

	// Deep-copy children so conversion never mutates the caller's source tree through shared refs.
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(RefExpander::DeepCopyNode(Child));

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	VisibleChildren.Reserve(Children.Num());
	for (const auto& Child : Children)
	{
		if (!Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(Child))
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

	// Use the first non-skipped frame name for WidgetName, then fall back to WBP_Generated.
	for (const auto& Child : VisibleChildren)
	{
		if (!Child->bReusable && !Child->Name.IsEmpty())
		{
			Result.WidgetName = FString::Printf(
				TEXT("%s%s"),
				Design2UMGGeneratedNameSuffixes::WidgetBlueprintPrefix(),
				*Child->Name);
			break;
		}
	}
	if (Result.WidgetName.IsEmpty())
		Result.WidgetName = TEXT("WBP_Generated");

	// Pre-compute fit-content node sizes by recursively resolving child-driven bounds.
	// Fit estimation works on stage copies of the per-conversion expansion
	// snapshots, leaving the cached expansion data immutable for conversion.
	// Ref expansions re-resolve SourceVar colors against the resolver, so the root
	// context starts from the document-level active theme like the parser does.
	const TMap<FString, FString> RootThemeContext = FDesignVariableResolver::BuildDocumentTheme(Document.Themes);
	const Design2UMGConverterInternal::FFlowChildPreResolutionResolver RootPreResolutionResolver(
		Document,
		RootThemeContext,
		State.RefExpansionCache,
		&State.SettingsSnapshot.FontMappings, PrepareFonts);
	const auto RefResolver =
		[&RootPreResolutionResolver](const TSharedPtr<FDesignNode>& RefNode)
		{
			return RootPreResolutionResolver.MakeWorkingCopy(RefNode);
		};
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable) continue;
		SizeEstimator::PromoteFitContentSizes(
			Child,
			Document,
			RefResolver,
			&State.SettingsSnapshot.FontMappings,
			&State.ConversionError);
		if (!State.ConversionError.IsEmpty())
		{
			break;
		}
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	TSharedPtr<FDesignNode> ConvertedRootSource;
	for (const auto& Child : VisibleChildren)
	{
		if (!State.ConversionError.IsEmpty())
		{
			break;
		}
		if (Child->bReusable)
			continue;

		Child->Transform.X = 0.0f;
		Child->Transform.Y = 0.0f;

		TSharedPtr<FDesignNode> ConvertedChild = Child;
		FUMGWidgetNode ChildWidget = NodeOrchestrator.Convert(ConvertedChild);
		if (!ChildWidget.Type.IsEmpty())
		{
			RenderWidgets.Add(MoveTemp(ChildWidget));
			ConvertedRootSource = ConvertedChild;
		}
	}

	if (State.PrototypeNavigation.IsValid())
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Convert: %d top-level widgets → prototype root with %d navigation bindings"),
			RenderWidgets.Num(),
			State.PrototypeNavigation.ClickBindings.Num());
		Result.RootWidget = Design2UMGPrototypeNavigation::BuildRoot(MoveTemp(RenderWidgets), State.PrototypeNavigation);
		FinalizeResult();
		return Result;
	}

	if (RenderWidgets.Num() == 1)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Convert: single root '%s' type=%s"), *RenderWidgets[0].Name, *RenderWidgets[0].Type);
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		if (State.ConversionError.IsEmpty() && Document.Metadata.FindRef(TEXT("source.provider")) == DesignComponentMapping::ProviderPencil)
		{
			// All visual, slot, effect and metadata readers have finished with the source tree.
			// Replay only for export bounds; the records keep original cache identities alive.
			for (const auto& Subtree : ExportSubtrees)
				Subtree.Parent->Children[Subtree.ChildIndex] = Subtree.ConvertedChild;
			Result.ExportBounds = DesignSourceExportBounds::Calculate(ConvertedRootSource,
				[&RootPreResolutionResolver](const TSharedPtr<FDesignNode>& Ref)
				{ return RootPreResolutionResolver.ResolveExpandedChild(Ref, true); });
		}
		FinalizeResult();
		return Result;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Convert: %d top-level widgets → CanvasPanel root"), RenderWidgets.Num());
	Result.RootWidget.Type = UMGWidgetTypes::CanvasPanel;
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	FinalizeResult();
	return Result;
}
