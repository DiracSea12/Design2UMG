// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "UMGTextPaintOwner.h"
#include "DesignComponentMapping.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignJsonUtils.h"
#include "FigmaParser.h"
#include "FigmaParserStyling.h"
#include "VisualMapperFillTransfer.h"
#include "UMGValueParser.h"
#include "DesignColorUtils.h"
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
#include "UMGPropertyKeys.h"
#include "UMGSchemaSourceExtensions.h"
#include "SizeEstimator.h"
#include "DesignSourceExportBounds.h"
#include "DesignNodeSemantics.h"
#include "TextMeasurement.h"
#include "RefExpander.h"
#include "PencilScript.h"
#include "Misc/EngineVersionComparison.h"

namespace Design2UMGConverterPrivate
{
	using namespace Design2UMGConverterInternal;
	namespace VisualSupport = Design2UMGConverterVisualSupport;

	void AppendSourceNodeWarnings(const TSharedPtr<FDesignNode>& Node, TArray<FString>& Warnings)
	{
		if (!Node.IsValid()) return;
		if (Node->Type == EDesignNodeType::Unknown && !Node->Source().SourceNodeType.IsEmpty())
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Node '%s' (%s) has unsupported source type '%s'."),
				*Node->Name, *Node->Id, *Node->Source().SourceNodeType));
		}
		if (Node->Type == EDesignNodeType::Path && Node->Shape.Geometry.IsEmpty()
			&& !Node->Shape.PaintGeometry.FillPaths.IsSet() && !Node->Shape.PaintGeometry.StrokePaths.IsSet()
			&& (Node->Visual.Fills.Num() > 0 || Node->Visual.Stroke.HasStroke()))
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Node '%s' (%s) has no vector geometry; its painted contour cannot be generated."),
				*Node->Name, *Node->Id));
		}
		for (const auto& Child : Node->Children)
		{
			AppendSourceNodeWarnings(Child, Warnings);
		}
	}

	TSharedPtr<FDesignNode> FindSourceNode(const TArray<TSharedPtr<FDesignNode>>& Roots, const FString& Id)
	{
		TSharedPtr<FDesignNode> Found;
		TArray<TSharedPtr<FDesignNode>> Pending = Roots;
		while (!Pending.IsEmpty())
		{
			auto Node = Pending.Pop();
			if (!Node) continue;
			if (Node->Id == Id)
			{
				if (Found) return nullptr;
				Found = Node;
			}
			Pending.Append(Node->Children);
		}
		return Found;
	}

	bool BuildSurfacePaints(const FDesignNode& Node, TArray<FUMGSurfacePaint>& OutPaints, FString& Error)
	{
		const auto Raw = Node.Source().Prototype.AnimationSource;
		const auto Fills = DesignJsonUtils::GetArray(Raw, TEXT("fills"));
		if (!Fills) { Error = TEXT("paint source is missing"); return false; }
		for (const auto& Value : *Fills)
		{
			const auto Paint = Value->AsObject();
			if (!Paint || !DesignJsonUtils::GetBool(Paint, TEXT("visible"), true)) continue;
			auto Copy = MakeShared<FJsonObject>();
			Copy->Values = Paint->Values;
			Copy->SetNumberField(TEXT("opacity"), 1.0);
			TArray<TSharedPtr<FJsonValue>> One{MakeShared<FJsonValueObject>(Copy)};
			const auto Parsed = FigmaParserInternal::ParseFills(One);
			if (Parsed.Num() != 1 || (Parsed[0].Type != EDesignFillType::Color && Parsed[0].Type != EDesignFillType::Gradient))
			{ Error = TEXT("paint requires a supported solid or gradient fill"); return false; }
			FUMGSurfacePaint& Out = OutPaints.AddDefaulted_GetRef();
			Out.Opacity = DesignJsonUtils::GetNumber(Paint, TEXT("opacity"), 1.0);
			Out.BlendMode = VisualMapperFillTransfer::CanonicalPaintBlendMode(Parsed[0].BlendMode);
			if (Parsed[0].Type == EDesignFillType::Color) Out.Color = FUMGValueParser::ParseColor(Parsed[0].SolidColor);
			else
			{
				FUMGVisualBrushData Brush;
				VisualMapperFillTransfer::ApplyGradientBrushData(Brush, Parsed[0].Gradient);
				Out.Gradient = MoveTemp(Brush.Gradient);
			}
		}
		return true;
	}

	bool BuildSharedCaptureEndpoint(const FDesignNode& Node, FUMGSharedCaptureEndpoint& Endpoint, FString& Error)
	{
		FUMGEffectData Contour;
		if (!VisualMapper::BuildSourceContour(Node, Contour, Error)) return false;
		Endpoint.Size = Contour.GeometryMaskSize.GetValue();
		Endpoint.bClipContent = Node.LayoutData.ExplicitClip.Get(false);
		Endpoint.CornerRadii = Node.Visual.CornerRadius.ToVector4();
		Endpoint.ClipPaths = Contour.GeometryMaskPaths;
		Endpoint.OwnPaint.Geometry.FillPaths = Contour.GeometryMaskPaths;
		Endpoint.OwnPaint.Geometry.StrokePaths = Node.Shape.PaintGeometry.StrokePaths.Get(TArray<FDesignGeometryPath>());
		Endpoint.OwnPaint.StrokeAlignment = Node.Visual.Stroke.Align == EDesignStrokeAlign::Inside ? TEXT("inner")
			: Node.Visual.Stroke.Align == EDesignStrokeAlign::Outside ? TEXT("outer") : TEXT("center");
		TArray<FUMGSurfacePaint> Paints;
		if (!BuildSurfacePaints(Node, Paints, Error)) return false;
		for (auto& Paint : Paints) { Paint.Color.A *= Paint.Opacity; Paint.Opacity = 1; }
		Endpoint.OwnPaint.FillPaints = MoveTemp(Paints);
		FUMGSurfacePaint Stroke;
		Stroke.Color = FLinearColor::Transparent;
		if (Node.Visual.Stroke.HasStroke() && Node.Visual.Stroke.Fill.IsSet())
		{
			const auto& Fill = Node.Visual.Stroke.Fill.GetValue();
			if (Fill.Type == EDesignFillType::Color) Stroke.Color = FUMGValueParser::ParseColor(Fill.SolidColor);
			else if (Fill.Type == EDesignFillType::Gradient)
			{
				FUMGVisualBrushData Brush;
				VisualMapperFillTransfer::ApplyGradientBrushData(Brush, Fill.Gradient);
				Stroke.Gradient = MoveTemp(Brush.Gradient); Stroke.Color = FLinearColor::White;
			}
			else { Error = TEXT("shared capture own stroke has no supported native paint"); return false; }
		}
		Endpoint.OwnPaint.StrokePaint = MoveTemp(Stroke);
		for (const auto& Effect : Node.Visual.Effects)
		{
			if (!Effect.bEnabled) continue;
			if (Effect.Type != EDesignEffectType::Shadow || Effect.Shadow.bInner)
			{ Error = TEXT("shared native capture cannot replace this source effect"); return false; }
			const auto& Shadow = Effect.Shadow;
			FUMGBoxShadowLayerData Layer;
			Layer.Offset = FVector2D(Shadow.OffsetX, Shadow.OffsetY); Layer.Blur = Shadow.Blur; Layer.Spread = Shadow.Spread;
			Layer.BlendMode = VisualMapperFillTransfer::CanonicalPaintBlendMode(Effect.BlendMode);
			Layer.Color = FUMGValueParser::ParseColor(Shadow.Color.IsEmpty() ? DesignColorUtils::DefaultShadowColorHex : Shadow.Color);
			Layer.bShowShadowBehindNode = Shadow.bShowShadowBehindNode;
			Endpoint.BoxShadows.Add(Layer);
		}
		if (!Endpoint.BoxShadows.IsEmpty() && Node.Visual.Stroke.HasStroke() && !Node.Shape.PaintGeometry.StrokePaths.IsSet())
		{ Error = TEXT("shared native shadow requires the complete source stroke region"); return false; }
		return true;
	}

	FUMGWidgetNode* SharedCaptureNode(FUMGWidgetNode& Owner)
	{
		for (auto& Child : Owner.Children)
			if (Child.Type == UMGWidgetTypes::RetainerBox && Child.Effect.SharedCapture.IsSet()) return &Child;
		return nullptr;
	}
	FUMGWidgetNode* SharedShadowNode(FUMGWidgetNode& Owner)
	{
		for (auto& Child : Owner.Children)
			if (Child.Type == UMGWidgetTypes::Image && Child.Effect.SharedCapture.IsSet()) return &Child;
		return nullptr;
	}
	FUMGWidgetNode* LogicalContent(FUMGWidgetNode& Owner)
	{
		if (auto* Capture = SharedCaptureNode(Owner)) return &Capture->Children[0].Children[0];
		return Owner.Children.IsEmpty() ? nullptr : &Owner.Children.Last();
	}

	void ScopeAddedSubtree(FDesignNode& Node, const FString& Prefix, const FString& RootId)
	{
		Node.Id = RootId.IsEmpty() ? Prefix + Node.Id : RootId;
		for (const auto& Child : Node.Children)
			if (Child) ScopeAddedSubtree(*Child, Prefix, FString());
	}

	void CollectLivePresentations(const FDesignNode& Destination, const FDesignNode& Presentation,
		const FUMGPrototypeNavigationData& Navigation, FUMGPrototypeTransitionData& Transition)
	{
		if (!Transition.Diagnostic.IsEmpty() || !Presentation.Transform.bEnabled) return;
		const auto* Host = Navigation.StateHosts.FindByPredicate(
			[&](const FUMGPrototypeStateHost& Item) { return Item.InstanceWidgetId == Destination.Id; });
		const bool bTrigger = Navigation.ClickBindings.ContainsByPredicate([&](const FUMGPrototypeNavigationBinding& Item)
			{ return Item.TriggerWidgetId == Destination.Id && !Item.Steps.IsEmpty() && Item.Transition.Diagnostic.IsEmpty(); });
		if (Host || bTrigger)
		{
			const FString Id = Host ? Host->HostWidgetId : Destination.Id;
			Transition.LivePresentations.Add({Id, Id + TEXT("__live_socket"), Presentation.Id, FString(), FString(), FString()});
			return;
		}
		if (Destination.Children.Num() != Presentation.Children.Num())
		{
			Transition.Diagnostic = TEXT("live transition subtree does not retain its canonical child hierarchy");
			return;
		}
		for (int32 Index = 0; Index < Destination.Children.Num(); ++Index)
			CollectLivePresentations(*Destination.Children[Index], *Presentation.Children[Index], Navigation, Transition);
	}

	void AttachPrototypeLayers(const TArray<TSharedPtr<FDesignNode>>& Roots,
		FUMGPrototypeNavigationData& Navigation, const TMap<FString, FString>& ImageUrls, TArray<FString>& Warnings)
	{
		TSet<FString> AddedIds;
		TMap<FString, TArray<FUMGPrototypeLivePresentation>> LiveLayers;
		struct FAttachment
		{
			TSharedPtr<FDesignNode> Parent;
			TSharedPtr<FDesignNode> Child;
			int32 ChildIndex;
		};
		TArray<FAttachment> Attachments;
		for (auto& Binding : Navigation.ClickBindings)
		{
			auto& Transition = Binding.Transition;
			TArray<TPair<TSharedPtr<FDesignNode>, TSharedPtr<FDesignNode>>> Staged;
			TSet<FString> StagedIds;
			for (const auto& Layer : Transition.AddedLayers)
			{
				if (Layer.bReplaceSource || Layer.bFlowChild) continue;
				if (AddedIds.Contains(Layer.NodeId))
				{
					Transition.LivePresentations.Append(LiveLayers.FindChecked(Layer.NodeId));
					continue;
				}
				if (StagedIds.Contains(Layer.NodeId)) continue;
				auto Parent = FindSourceNode(Roots, Layer.ParentNodeId);
				TSharedPtr<FJsonObject> Source;
				FDesignDocument Parsed;
				if (!Parent || !DesignJsonUtils::TryParseObject(Layer.SourceNodeJson, Source))
				{
					Transition.Diagnostic = TEXT("cannot resolve added transition layer parent or source");
					break;
				}
				auto Envelope = MakeShared<FJsonObject>();
				Envelope->SetObjectField(TEXT("document"), Source);
				if (!FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Envelope), Parsed)
					|| Parsed.Children.Num() != 1 || !Parsed.Children[0])
				{
					Transition.Diagnostic = TEXT("cannot parse added transition layer");
					break;
				}
				FFigmaParser::ApplyImageUrls(Parsed, ImageUrls);
				auto Added = Parsed.Children[0];
				ScopeAddedSubtree(*Added, Transition.LayerScopeId() + TEXT("::"), Layer.NodeId);
				const auto Destination = FindSourceNode(Roots, Layer.DestinationNodeId);
				if (!Destination)
				{
					Transition.Diagnostic = TEXT("added live transition layer has no canonical destination");
					break;
				}
				const int32 FirstLive = Transition.LivePresentations.Num();
				CollectLivePresentations(*Destination, *Added, Navigation, Transition);
				TArray<FUMGPrototypeLivePresentation>& Live = LiveLayers.FindOrAdd(Layer.NodeId);
				Live.Reset();
				for (int32 Index = FirstLive; Index < Transition.LivePresentations.Num(); ++Index) Live.Add(Transition.LivePresentations[Index]);
				Added->Transform.X = Layer.RelativePosition.X;
				Added->Transform.Y = Layer.RelativePosition.Y;
				Added->Transform.Opacity = 0;
				Added->LayoutData.bAbsoluteLayoutPositioning = true;
				Staged.Emplace(Parent, Added);
				StagedIds.Add(Layer.NodeId);
			}
			if (Transition.Diagnostic.IsEmpty())
			{
				for (auto& Pair : Staged)
				{
					const auto* Layer = Transition.AddedLayers.FindByPredicate(
						[&Pair](const FUMGPrototypeAddedLayer& Item) { return Item.NodeId == Pair.Value->Id; });
					Attachments.Add({Pair.Key, Pair.Value,
						Pair.Key->LayoutData.Layout == EDesignLayout::None && Layer ? Layer->DestinationChildIndex : INDEX_NONE});
				}
				for (const FString& Id : StagedIds) AddedIds.Add(Id);
			}
			else
			{
				Transition.WidgetTargets.Reset();
				Transition.LayerOrders.Reset();
				Transition.LivePresentations.Reset();
				Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s': %s; action not executed"),
					*Binding.TriggerWidgetId, *Transition.Diagnostic));
			}
			if (Binding.Steps.Num() == 1) Binding.Steps[0].Transition = Transition;
		}
		// Resolve every canonical subtree before inserting any generated presentation children.
		for (auto& Attachment : Attachments)
			if (Attachment.ChildIndex == INDEX_NONE) Attachment.Parent->Children.Add(Attachment.Child);
			else Attachment.Parent->Children.Insert(Attachment.Child, FMath::Clamp(Attachment.ChildIndex, 0, Attachment.Parent->Children.Num()));
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
			const EDesignLayout ParentLayout, const FString* SurfacePaintOwnerId = nullptr) const
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

			const FSurfaceRoles Roles = ResolveSurfaceRoles(*Node, Context.State, SurfacePaintOwnerId ? *SurfacePaintOwnerId : Node->Id);
			TSharedPtr<FDesignNode> LayoutNode = Node;
			if ((Node->LayoutData.Layout == EDesignLayout::Horizontal || Node->LayoutData.Layout == EDesignLayout::Vertical
				|| Node->LayoutData.Layout == EDesignLayout::Wrap) && Node->Children.Num() > 1
				&& (Node->LayoutData.bReversePaintOrder || Context.State.IndependentPaintParentIds.Contains(Node->Id)))
			{
				// Fixed source coordinates preserve flow placement while Canvas children own paint rank.
				LayoutNode = MakeShared<FDesignNode>(*Node);
				LayoutNode->LayoutData.Layout = EDesignLayout::None;
				LayoutNode->LayoutData.ExplicitPadding.Reset();
				LayoutNode->LayoutData.PaddingRequiresRuntimeWrap = false;
				LayoutNode->LayoutData.ExplicitGap.Reset();
				LayoutNode->LayoutData.JustifyContent = EDesignJustifyContent::Start;
				Context.State.IndependentPaintParentIds.Add(Node->Id);
			}
			const TSharedPtr<FDesignNode> VisualNode = Roles.bSurfacePaint || Roles.bSourceContour || Roles.bLocalSurface
				? BuildSurfaceVisualNode(*LayoutNode, Roles) : LayoutNode;
			FUMGWidgetNode Widget = CreateBaseWidgetForNode(VisualNode, Context.State);
			LogCreatedWidget(Widget, Node);
			VisualSupport::ApplyBaseVisualProperties(Widget, VisualNode);
			if (Roles.bSourceContour || Roles.bLocalSurface) Widget.Brush.BrushColor = FLinearColor::Transparent;
			TArray<FUMGWidgetNode> AbsoluteChildren;
			AppendChildWidgets(Widget, LayoutNode, ActiveContext, PreResolutionResolver, AbsoluteChildren);
			ApplyLayoutAdjustments(Widget, VisualNode, ActiveContext, ParentLayout, MoveTemp(AbsoluteChildren), Roles.bSourceContour);
			if (!ApplySurfaceRoles(Widget, Node, ActiveContext, Roles)) return FUMGWidgetNode();
			Widget = WrapWithPrototypeNavigation(MoveTemp(Widget), Node, ActiveContext);
			Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);

			return Widget;
		}

		/** Animation surface roles that change how one node is painted. */
		struct FSurfaceRoles
		{
			bool bSharedCapture = false;
			bool bLocalSurface = false;
			bool bSurfacePaint = false;
			bool bPathSurfacePaint = false;
			bool bSourceContour = false;
		};

		static FSurfaceRoles ResolveSurfaceRoles(const FDesignNode& Node, const FConversionState& State, const FString& SurfaceOwnerId)
		{
			const TArray<FUMGPrototypeNavigationBinding>& Bindings = State.PrototypeNavigation.ClickBindings;
			FSurfaceRoles Roles;
			Roles.bSharedCapture = Bindings.ContainsByPredicate(
				[&](const FUMGPrototypeNavigationBinding& Binding)
				{ return Binding.Transition.SharedCaptures.ContainsByPredicate([&](const FUMGPrototypeSharedCapture& Capture)
					{ return Capture.NodeId == Node.Id || Capture.DestinationWidgetId == Node.Id; }); });
			Roles.bLocalSurface = Roles.bSharedCapture || Bindings.ContainsByPredicate(
				[&](const FUMGPrototypeNavigationBinding& Binding)
				{ return Binding.Transition.LocalEndpoints.ContainsByPredicate([&](const FUMGPrototypeLocalEndpoint& Endpoint)
					{ return !Endpoint.bText && (Endpoint.NodeId == Node.Id || Endpoint.DestinationWidgetId == Node.Id); }); });
			Roles.bSurfacePaint = !Roles.bLocalSurface && Node.Type != EDesignNodeType::Text && Bindings.ContainsByPredicate(
				[&](const FUMGPrototypeNavigationBinding& Binding)
				{
					return Binding.Transition.WidgetTargets.ContainsByPredicate([&](const FUMGPrototypeWidgetAnimationTarget& Target)
					{ return Target.bSurfacePaintChange && (Target.NodeId == SurfaceOwnerId || Target.DestinationWidgetId == SurfaceOwnerId); });
				});
			Roles.bPathSurfacePaint = Roles.bSurfacePaint && (Node.Type == EDesignNodeType::Path || Node.Type == EDesignNodeType::Polygon)
				&& Node.Shape.PaintGeometry.FillPaths.IsSet() && !Node.Shape.PaintGeometry.FillPaths->IsEmpty();
			Roles.bSourceContour = !Roles.bLocalSurface && VisualMapper::RequiresSourceContour(Node);
			return Roles;
		}

		/** The base widget skips paint that a surface layer draws instead. */
		static TSharedPtr<FDesignNode> BuildSurfaceVisualNode(const FDesignNode& LayoutNode, const FSurfaceRoles& Roles)
		{
			TSharedPtr<FDesignNode> VisualNode = MakeShared<FDesignNode>(LayoutNode);
			VisualNode->Visual.Fills.Reset();
			if (Roles.bPathSurfacePaint)
			{
				VisualNode->Shape = FDesignShapeNodeData();
				VisualNode->Type = EDesignNodeType::Frame;
			}
			if (Roles.bLocalSurface) VisualNode->Visual.Effects.Reset();
			if (Roles.bSourceContour || Roles.bLocalSurface)
			{
				if (Roles.bLocalSurface)
				{
					VisualNode->Shape = FDesignShapeNodeData();
					if (VisualNode->Type == EDesignNodeType::Path) VisualNode->Type = EDesignNodeType::Frame;
				}
				VisualNode->Visual.CornerRadius = FDesignCornerRadius();
				VisualNode->Visual.Stroke = FDesignStroke();
			}
			return VisualNode;
		}

		/**
		 * Wraps a converted node in its animation surface layers.
		 * Kept out of ConvertNode so the recursive conversion frame stays small at legal nesting depths.
		 */
		FORCENOINLINE bool ApplySurfaceRoles(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context, const FSurfaceRoles& Roles) const
		{
			FString& Error = Context.State.ConversionError;
			FUMGEffectData Contour;
			if ((Roles.bSourceContour || Roles.bPathSurfacePaint) && !VisualMapper::BuildSourceContour(*Node, Contour, Error)) return false;
			FUMGWidgetNode SourceSurface;
			if (Roles.bLocalSurface)
			{
				TSharedPtr<FDesignNode> PaintNode = MakeShared<FDesignNode>(*Node);
				PaintNode->Id += TEXT("__local_surface");
				PaintNode->Children.Reset();
				if (Roles.bSharedCapture) { PaintNode->Visual.Effects.Reset(); PaintNode->LayoutData.ExplicitClip = false; }
				PaintNode->Transform.SetPosition(0, 0); PaintNode->Transform.SourceLayoutPosition.Reset();
				PaintNode->Transform.SetOpacityValue(1); PaintNode->Transform.Rotation = 0;
				PaintNode->Transform.bFlipX = PaintNode->Transform.bFlipY = false;
				if (!Roles.bSharedCapture || Design2UMGCore::DesignNodeSemantics::HasRenderableVisual(PaintNode))
				{
					SourceSurface = ConvertNode(PaintNode, Context, EDesignLayout::None, &Node->Id);
					if (SourceSurface.Type.IsEmpty()) return false;
					SourceSurface.MutableSource() = FUMGSourceNodeData();
				}
			}
			FUMGShadowData ContourShadows;
			if (Roles.bSourceContour)
			{
				ContourShadows = MoveTemp(Widget.Shadow);
				Widget.Shadow = FUMGShadowData();
			}
			if (Roles.bSurfacePaint)
			{
				TArray<FUMGSurfacePaint> Paints;
				if (!BuildSurfacePaints(*Node, Paints, Error)) return false;
				FUMGWidgetNode Surface;
				Surface.Type = UMGWidgetTypes::Image;
				Surface.Id = Widget.Id + TEXT("__surface_paint");
				Surface.Name = Widget.Name + TEXT("__surface_paint");
				Surface.Bounds = Widget.Bounds;
				Surface.Bounds.X = Surface.Bounds.Y = 0;
				Surface.Brush.MaterialRef = TEXT("native-surface-paints");
				Surface.Brush.BrushColor = FLinearColor::White;
				Surface.Effect.SurfacePaints = MoveTemp(Paints);
				Surface.Effect.GeometryMaskShape = Roles.bSourceContour ? EUMGGeometryMaskShape::Rectangle
					: (Node->Type == EDesignNodeType::Ellipse ? EUMGGeometryMaskShape::Ellipse : EUMGGeometryMaskShape::RoundedRectangle);
				Surface.Effect.GeometryMaskSize = FVector2D(Widget.Bounds.W, Widget.Bounds.H);
				if (Roles.bPathSurfacePaint || (Roles.bSourceContour && !Node->LayoutData.ExplicitClip.Get(false)))
				{
					Surface.Effect.GeometryMaskPaths = Contour.GeometryMaskPaths;
					Surface.Effect.GeometryMaskSize = Contour.GeometryMaskSize;
				}
				const auto& Radius = Node->Visual.CornerRadius;
				Surface.Effect.GeometryMaskCornerRadii = Radius.bUniform ? FVector4(Radius.Uniform, Radius.Uniform, Radius.Uniform, Radius.Uniform) : FVector4(Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft);
				Surface.Slot.Anchors.Max.X = Surface.Slot.Anchors.Max.Y = 1;
				Surface.Slot.Offsets.Right = Surface.Slot.Offsets.Bottom = 0;
				Surface.WidgetStyle.Visibility = TEXT("HitTestInvisible");
				FUMGWidgetNode Layer;
				Layer.Type = UMGWidgetTypes::Overlay;
				Layer.Id = Widget.Id;
				Layer.Name = Widget.Name;
				Layer.Bounds = Widget.Bounds;
				Layer.Slot = Widget.Slot;
				Layer.Render = MoveTemp(Widget.Render);
				Layer.WidgetStyle = MoveTemp(Widget.WidgetStyle);
				Widget.Render = FUMGRenderStyleData();
				Widget.WidgetStyle = FUMGWidgetStyleData();
				Widget.Id += TEXT("__surface_content");
				Widget.Name += TEXT("__surface_content");
				Widget.Bounds.X = Widget.Bounds.Y = 0;
				Widget.Slot = Surface.Slot;
				Layer.Children.Add(MoveTemp(Surface));
				Layer.Children.Add(MoveTemp(Widget));
				Widget = MoveTemp(Layer);
			}
			if (Roles.bSourceContour)
			{
				auto ContourNode = MakeShared<FDesignNode>(*Node);
				ContourNode->Shape.ViewBox = FVector4(0, 0, Contour.GeometryMaskSize->X, Contour.GeometryMaskSize->Y);
				ContourNode->Shape.PaintGeometry.FillPaths = Contour.GeometryMaskPaths;
				int32 PaintLayerCount = Roles.bSurfacePaint ? 1 : 0;
				if (!Roles.bSurfacePaint) Widget = VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(Widget), ContourNode,
					Context.State.SettingsSnapshot.VisualMapperOptions, &PaintLayerCount);
				if (Widget.Children.IsValidIndex(PaintLayerCount))
				{
					Widget.Children[PaintLayerCount].Id = Node->Id + TEXT("__contour_layout");
					Widget.Children[PaintLayerCount].Name = Widget.Name + TEXT("__contour_layout");
				}
				if (Node->LayoutData.ExplicitClip.Get(false))
					Widget = VisualMapper::WrapWithSourceContour(MoveTemp(Widget), Contour);
				else if (!Roles.bSurfacePaint && !VisualMapperFillTransfer::PrepareSourceContourPaint(Widget, Contour, PaintLayerCount, Error))
					return false;
				Widget.Shadow = MoveTemp(ContourShadows);
				Widget = VisualSupport::ApplyVisualEffectWrappers(MoveTemp(Widget), ContourNode, Context.State.SettingsSnapshot.VisualMapperOptions, false, false, true);
			}
			if (Roles.bLocalSurface)
			{
				FUMGWidgetNode Layer;
				Layer.Type = UMGWidgetTypes::Overlay; Layer.Id = Widget.Id; Layer.Name = Widget.Name;
				Layer.Bounds = Widget.Bounds; Layer.Slot = Widget.Slot;
				Layer.Render = MoveTemp(Widget.Render); Layer.WidgetStyle.Visibility = Widget.WidgetStyle.Visibility;
				if (!SourceSurface.Type.IsEmpty())
				{
					SourceSurface.Slot = FUMGSlotData(); SourceSurface.WidgetStyle.Visibility = TEXT("HitTestInvisible");
					Layer.SourceVisualChildIndex = Layer.Children.Add(MoveTemp(SourceSurface));
				}
				Widget.Id += TEXT("__local_content"); Widget.Name += TEXT("__local_content");
				Widget.Bounds.X = Widget.Bounds.Y = 0; Widget.Slot = FUMGSlotData();
				Widget.Render = FUMGRenderStyleData(); Widget.WidgetStyle.Visibility.Reset();
				Layer.Children.Add(MoveTemp(Widget));
				Widget = MoveTemp(Layer);
			}
			if (Roles.bSharedCapture)
			{
				FUMGSharedCaptureEndpoint Endpoint;
				if (!BuildSharedCaptureEndpoint(*Node, Endpoint, Error)) return false;
				FUMGWidgetNode Content = MoveTemp(Widget.Children.Last()); Widget.Children.Pop();
				Content.Render.Clipping = Widget.Render.Clipping;
				Widget.Render.Clipping.Reset();
				Content.Bounds.X = Content.Bounds.Y = 0;
				Content.Slot = FUMGSlotData(); Content.Slot.Offsets = {0.0f, 0.0f, static_cast<float>(Endpoint.Size.X), static_cast<float>(Endpoint.Size.Y)};
				FUMGWidgetNode Canvas;
				Canvas.Type = UMGWidgetTypes::CanvasPanel; Canvas.Id = Node->Id + TEXT("__capture_canvas"); Canvas.Name = Canvas.Id;
				Canvas.Bounds.W = Endpoint.Size.X; Canvas.Bounds.H = Endpoint.Size.Y;
				Canvas.Children.Add(MoveTemp(Content));
				FUMGWidgetNode Capture;
				Capture.Type = UMGWidgetTypes::RetainerBox; Capture.Id = Node->Id + TEXT("__shared_capture"); Capture.Name = Capture.Id;
				Capture.Bounds.W = Endpoint.Size.X; Capture.Bounds.H = Endpoint.Size.Y;
				Capture.Effect.SharedCapture.Emplace(); Capture.Effect.SharedCapture->Source = Endpoint;
				Capture.Effect.EffectMaterial = UMGPropertyKeys::RetainerSharedCaptureEffectRef;
				Capture.Effect.bRetainRendering = true; Capture.Effect.TextureParameter = TEXT("Texture");
				Capture.WidgetStyle.Visibility = TEXT("SelfHitTestInvisible");
				Capture.Children.Add(MoveTemp(Canvas));
				FUMGWidgetNode Shadow;
				Shadow.Type = UMGWidgetTypes::Image; Shadow.Id = Node->Id + TEXT("__shared_shadow"); Shadow.Name = Shadow.Id;
				Shadow.Bounds.W = Endpoint.Size.X; Shadow.Bounds.H = Endpoint.Size.Y;
				Shadow.Brush.BrushColor = FLinearColor::White;
				Shadow.Effect.SharedCapture = Capture.Effect.SharedCapture;
				Shadow.WidgetStyle.Visibility = TEXT("HitTestInvisible");
				Widget.Children.Insert(MoveTemp(Shadow), 0);
				if (Widget.SourceVisualChildIndex != INDEX_NONE) ++Widget.SourceVisualChildIndex;
				Widget.Children.Add(MoveTemp(Capture));
			}
			return true;
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
						const bool bTargetOnly = Context.State.PrototypeNavigation.ClickBindings.ContainsByPredicate(
							[&EffectiveChild](const FUMGPrototypeNavigationBinding& Binding)
							{
								return Binding.Transition.AddedLayers.ContainsByPredicate(
									[&EffectiveChild](const FUMGPrototypeAddedLayer& Layer)
									{ return !Layer.bReplaceSource && Layer.NodeId == EffectiveChild->Id; });
							});
						if (bTargetOnly) AbsoluteWidget.WidgetStyle.Visibility = TEXT("HitTestInvisible");
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
			if (Node->LayoutData.Layout == EDesignLayout::None)
			{
				for (auto& Child : Widget.Children)
				{
					if (Context.State.PrototypeNavigation.ClickBindings.ContainsByPredicate([&](const FUMGPrototypeNavigationBinding& Binding)
					{
						return Binding.Transition.AddedLayers.ContainsByPredicate([&](const FUMGPrototypeAddedLayer& Layer)
						{ return !Layer.bReplaceSource && (Child.Id == Layer.NodeId || (Child.Type == UMGWidgetTypes::SizeBox
							&& Child.Children.Num() == 1 && Child.Children[0].Id == Layer.NodeId)); });
					})) Child.WidgetStyle.Visibility = TEXT("HitTestInvisible");
				}
			}
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

			const bool bIndependentPaintOrder = Widget.Type == UMGWidgetTypes::CanvasPanel
				&& Context.State.IndependentPaintParentIds.Contains(Node->Id);
			int32 SourceChildCount = Node->Children.Num();
			if (bIndependentPaintOrder)
				for (const auto& Child : Node->Children)
					if (Child && Context.State.AddedLayerPaintRanks.Contains(Child->Id)) --SourceChildCount;
			int32 AddedBefore = 0;
			int32 VisibleIndex = 0;
			for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num() && VisibleIndex < VisibleChildren.Num(); ++ChildIndex)
			{
				const auto& SourceChild = Node->Children[ChildIndex];
				const int32* AddedRank = SourceChild ? Context.State.AddedLayerPaintRanks.Find(SourceChild->Id) : nullptr;
				const int32 SourceChildIndex = ChildIndex - AddedBefore;
				if (AddedRank) ++AddedBefore;
				if (SourceChild != VisibleChildren[VisibleIndex]) continue;
				const TSharedPtr<FDesignNode> Child = VisibleChildren[VisibleIndex++];
				TSharedPtr<FDesignNode> EffectiveChild = PreResolutionResolver.ResolveExpandedChild(Child, true);
				if (bIndependentPaintOrder && !EffectiveChild->LayoutData.bAbsoluteLayoutPositioning)
				{
					if (!EffectiveChild->Transform.SourceLayoutPosition.IsSet())
					{
						Context.State.ConversionError = FString::Printf(TEXT("independent child paint order requires a parsed source layout position for '%s' (%s)"),
							*EffectiveChild->Name, *EffectiveChild->Id);
						return;
					}
					EffectiveChild = MakeShared<FDesignNode>(*EffectiveChild);
					const FVector2D Position = EffectiveChild->Transform.SourceLayoutPosition.GetValue();
					EffectiveChild->Transform.SetPosition(Position.X, Position.Y);
				}
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

				const int32 PaintRank = AddedRank ? *AddedRank : Node->LayoutData.ChildPaintRank(SourceChildIndex, SourceChildCount);
				if (bIndependentPaintOrder)
				{
					ChildWidget.Slot.ZOrder = PaintRank;
					Context.State.PaintOrderWidgetIds.Add(Node->Id);
					Context.State.PaintOrderWidgetIds.Add(ChildWidget.Id);
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

		void ApplyLayoutAdjustments(
			FUMGWidgetNode& Widget,
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout,
			TArray<FUMGWidgetNode> AbsoluteChildren, bool bDeferEffects) const
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

			bool bExplicitStrokePainted = false;
			const bool bPlaceholderUsesDirectImage = VisualSupport::TryApplyPlaceholderSurfaceTint(Widget, Node, VisualSettings);
			if (VisualSupport::ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
					*Widget.Name, VisualSupport::HasVisualFill(Node), Node->Visual.CornerRadius.HasRadius(), Node->Visual.Stroke.HasStroke());
				Widget = VisualSupport::WrapWithBorder(MoveTemp(Widget), Node, bExplicitStrokePainted);
			}
			else if (LayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
			{
				LayoutMapper::ApplyPadding(Widget, Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}));
			}

			if (!AbsoluteChildren.IsEmpty())
			{
				// Padding remains inside the flow subtree; absolute coordinates use the parent origin.
				FUMGWidgetNode Canvas;
				Canvas.Type = UMGWidgetTypes::CanvasPanel;
				Canvas.Id = Widget.Id;
				Canvas.Name = Widget.Name;
				Canvas.Bounds = Widget.Bounds;
				Canvas.Slot = Widget.Slot;
				Canvas.Render = MoveTemp(Widget.Render);
				Widget.Render = FUMGRenderStyleData();
				Widget.Id += TEXT("__flow");
				Widget.Name += TEXT("_Flow");
				Widget.Bounds.X = Widget.Bounds.Y = 0.0f;
				Widget.Slot = FUMGSlotData();
				Widget.Slot.Anchors.Max.X = Widget.Slot.Anchors.Max.Y = 1.0f;
				Widget.Slot.Offsets.Right = Widget.Slot.Offsets.Bottom = 0.0f;
				TArray<FUMGWidgetNode> FrontAbsoluteChildren;
				for (auto& Absolute : AbsoluteChildren)
				{
					const bool bBehindFlow = Context.State.PrototypeNavigation.ClickBindings.ContainsByPredicate(
						[&Absolute](const FUMGPrototypeNavigationBinding& Binding)
						{
							return Binding.Transition.AddedLayers.ContainsByPredicate(
								[&Absolute](const FUMGPrototypeAddedLayer& Layer)
								{ return Layer.NodeId == Absolute.Id && Layer.bBehindFlow; });
						});
					if (bBehindFlow) Canvas.Children.Add(MoveTemp(Absolute));
					else FrontAbsoluteChildren.Add(MoveTemp(Absolute));
				}
				Canvas.Children.Add(MoveTemp(Widget));
				Canvas.Children.Append(MoveTemp(FrontAbsoluteChildren));
				Widget = MoveTemp(Canvas);
			}

			const bool bAnimatedTextPaint = Node->Type == EDesignNodeType::Text && Context.State.PrototypeNavigation.ClickBindings.ContainsByPredicate(
				[&Node](const FUMGPrototypeNavigationBinding& Binding)
				{
					return Binding.Transition.WidgetTargets.ContainsByPredicate([&Node](const FUMGPrototypeWidgetAnimationTarget& Target)
					{ return Target.bTextPaintChange && (Target.NodeId == Node->Id || Target.DestinationWidgetId == Node->Id); });
				});
			const bool bGradientText = Node->Type == EDesignNodeType::Text && Widget.Type == UMGWidgetTypes::TextBlock
				&& Node->Source().Prototype.AnimationSource.IsValid()
				&& Node->Visual.Fills.ContainsByPredicate(
				[](const FDesignFill& Fill) { return Fill.bEnabled && Fill.Type == EDesignFillType::Gradient; });
			const bool bTextPaint = bAnimatedTextPaint || bGradientText;
			if (bTextPaint)
			{
				TArray<FUMGSurfacePaint> Paints;
				if (Widget.Type != UMGWidgetTypes::TextBlock || !Design2UMGConverterPrivate::BuildSurfacePaints(*Node, Paints, Context.State.ConversionError))
				{
					if (Context.State.ConversionError.IsEmpty()) Context.State.ConversionError = TEXT("text paint requires one native TextBlock");
					return;
				}
				FUMGWidgetNode Retainer;
				Retainer.Type = UMGWidgetTypes::RetainerBox;
				Retainer.Id = Widget.Id;
				Retainer.Name = Widget.Name;
				Retainer.Bounds = Widget.Bounds;
				Retainer.Slot = Widget.Slot;
				Retainer.Render = MoveTemp(Widget.Render);
				const auto TextPadding = Widget.WidgetStyle.Padding;
				Retainer.WidgetStyle = MoveTemp(Widget.WidgetStyle);
				Retainer.WidgetStyle.Padding.Reset();
				Retainer.Shadow = MoveTemp(Widget.Shadow);
				Widget.Shadow = FUMGShadowData();
				Widget.Render = FUMGRenderStyleData();
				Widget.WidgetStyle = FUMGWidgetStyleData();
				Widget.WidgetStyle.Padding = TextPadding;
				Retainer.Effect.bRetainRendering = true;
				Retainer.Effect.EffectMaterial = TEXT("native-text-paints");
				Retainer.Effect.TextureParameter = TEXT("Texture");
				Retainer.Effect.TextPaints = MoveTemp(Paints);
				Widget.Id += TEXT("__text_content");
				Widget.Name += TEXT("__text_content");
				Widget.Bounds = FDesignPixelBounds();
				Widget.Slot = FUMGSlotData();
				Widget.Text.Color = FLinearColor::White;
				Retainer.Children.Add(MoveTemp(Widget));
				Widget = MoveTemp(Retainer);
			}
			if (!bDeferEffects) Widget = VisualSupport::ApplyVisualEffectWrappers(MoveTemp(Widget), Node, VisualSettings.VisualMapperOptions, bExplicitStrokePainted, bTextPaint, false);
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
				FUMGWidgetNode Button = Design2UMGPrototypeNavigation::WrapWithButton(MoveTemp(Widget), Node);
				const bool bAnimatedOpacity = Context.State.PrototypeNavigation.ClickBindings.ContainsByPredicate(
					[&Node](const FUMGPrototypeNavigationBinding& Binding)
					{
						return Binding.Transition.WidgetTargets.ContainsByPredicate([&Node](const FUMGPrototypeWidgetAnimationTarget& Target)
						{
							return Target.NodeId == Node->Id && Target.bOpacity;
						});
					});
				if (bAnimatedOpacity)
				{
					Button.Render.RenderOpacity = Button.Children[0].Render.RenderOpacity;
					Button.Children[0].Render.RenderOpacity.Reset();
				}
				return Button;
			}
			return MoveTemp(Widget);
		}

		FUMGWidgetNode CreateBaseWidgetForNode(const TSharedPtr<FDesignNode>& Node, FConversionState& State) const
		{
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

	// Removes coordinate-only source canvases and generated size carriers that no prototype binding addresses.
	void FlattenWidgetTree(FUMGWidgetData& Result, const Design2UMGConverterInternal::FConversionState& State)
	{
		Design2UMGCanvasFlattening::FFlattenStats FlattenStats;
		TSet<FString> ReferencedWidgetIds;
		const auto Keep = [&](const FString& Id) { if (!Id.IsEmpty()) ReferencedWidgetIds.Add(Id); };
		const auto KeepTransition = [&](const FUMGPrototypeTransitionData& Transition)
		{
			Keep(Transition.StateHostWidgetId); Keep(Transition.SourceStateWidgetId); Keep(Transition.TargetStateWidgetId);
			Keep(Transition.LayoutSourceWidgetId); Keep(Transition.LayoutDestinationWidgetId);
			for (const auto& Endpoint : Transition.LocalEndpoints)
			{ Keep(Endpoint.NodeId); Keep(Endpoint.DestinationWidgetId); Keep(Endpoint.SourceVisualWidgetId); Keep(Endpoint.TargetVisualWidgetId);
				Keep(Endpoint.SourcePresentationWidgetId); Keep(Endpoint.TargetPresentationWidgetId); }
			for (const auto& Capture : Transition.SharedCaptures)
			{ Keep(Capture.NodeId); Keep(Capture.DestinationWidgetId); Keep(Capture.CaptureWidgetId); Keep(Capture.ShadowWidgetId);
				Keep(Capture.ContentWidgetId); Keep(Capture.PresentationCaptureWidgetId); Keep(Capture.PresentationShadowWidgetId); Keep(Capture.PresentationContentWidgetId); }
			Keep(Transition.LayoutOriginalWidgetId); Keep(Transition.LayoutPresentationWidgetId); Keep(Transition.LayoutSourcePageId);
			for (const auto& Target : Transition.WidgetTargets)
			{ Keep(Target.NodeId); Keep(Target.DestinationWidgetId); }
			for (const auto& Target : Transition.LayoutTargets)
			{ Keep(Target.NodeId); Keep(Target.DestinationWidgetId); Keep(Target.PresentationWidgetId); }
			for (const auto& Order : Transition.LayerOrders)
			{ Keep(Order.NodeId); Keep(Order.ParentNodeId); }
			for (const auto& Layer : Transition.AddedLayers)
			{ Keep(Layer.NodeId); Keep(Layer.ParentNodeId); }
			for (const auto& Live : Transition.LivePresentations)
			{ Keep(Live.WidgetId); Keep(Live.OriginalSocketWidgetId); Keep(Live.PresentationSocketWidgetId); }
		};
		const auto& Navigation = Result.PrototypeNavigation();
		Keep(Navigation.PageHostWidgetId); Keep(Navigation.OverlayHostWidgetId);
		for (const auto& Binding : Navigation.ClickBindings)
		{
			Keep(Binding.TriggerWidgetId); Keep(Binding.SourcePageId); KeepTransition(Binding.Transition);
			for (const auto& Step : Binding.Steps) { Keep(Step.TargetPageId); KeepTransition(Step.Transition); }
		}
		for (const auto& Host : Navigation.StateHosts)
		{
			Keep(Host.HostWidgetId); Keep(Host.InstanceWidgetId);
			for (const auto& Entry : Host.States) Keep(Entry.WidgetId);
		}
		for (const FString& Id : State.IndependentPaintParentIds) Keep(Id);
		for (const FString& Id : State.PaintOrderWidgetIds) Keep(Id);
		Design2UMGCanvasFlattening::FlattenBareNestedCanvasPanels(Result.RootWidget, ReferencedWidgetIds, FlattenStats);
		if (FlattenStats.MergedNestedCanvasCount > 0 || FlattenStats.RemovedEmptyCanvasCount > 0
			|| FlattenStats.RemovedRedundantSizeCarrierCount > 0)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("Convert: flattened %d nested bare canvas panels, removed %d empty canvas panels and %d redundant size carriers"),
				FlattenStats.MergedNestedCanvasCount, FlattenStats.RemovedEmptyCanvasCount, FlattenStats.RemovedRedundantSizeCarrierCount);
		}
	}
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
	Settings.bPreserveSourceNodes = SourceSettings->bPreserveSourceNodes;
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

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& InputDocument, const FDesign2UMGConversionSettings& Settings,
	const FPrepareFonts& PrepareFonts)
{
	FUMGWidgetData Result;
	FDesignDocument ExpandedDocument = InputDocument;
	const bool bAddedPrototypePages = Design2UMGPrototypeNavigation::AddReachablePages(ExpandedDocument, Result.Warnings);
	const FDesignDocument& Document = ExpandedDocument;
	for (const auto& Child : Document.Children)
	{
		Design2UMGConverterPrivate::AppendSourceNodeWarnings(Child, Result.Warnings);
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
	auto FinalizeResult = [&Result, &State, &Document, &Settings]()
	{
		if (!State.ConversionError.IsEmpty())
		{
			Result.RootWidget = FUMGWidgetNode();
			Result.ConversionError = State.ConversionError;
			return;
		}

		if (!Settings.bPreserveSourceNodes) Design2UMGConverterPrivate::FlattenWidgetTree(Result, State);
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

	TMap<FString, TSharedPtr<FDesignNode>> StateSourceTrees;
	{
		const Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis PrototypeAnalysis =
			Design2UMGPrototypeNavigation::BuildAnalysis(Document, VisibleChildren, Result.Warnings);
		State.PrototypeNavigation = PrototypeAnalysis.Data;
		TSet<FString> AnimatedIds;
		for (const auto& Binding : State.PrototypeNavigation.ClickBindings)
		{
			for (const auto& Target : Binding.Transition.WidgetTargets) AnimatedIds.Append({Target.NodeId, Target.DestinationWidgetId});
			for (const auto& Target : Binding.Transition.LayoutTargets) AnimatedIds.Append({Target.NodeId, Target.DestinationWidgetId});
		}
		const TFunction<void(const TSharedPtr<FDesignNode>&)> MarkAnimated = [&](const TSharedPtr<FDesignNode>& Node)
		{
			if (AnimatedIds.Contains(Node->Id)) Node->MutableSource().Prototype.bAnimatedSurface = true;
			for (const auto& Child : Node->Children) if (Child.IsValid()) MarkAnimated(Child);
		};
		for (const auto& Child : VisibleChildren) MarkAnimated(Child);
		// A fixed presentation retains a shared owner even when this action changes only its descendants.
		TSet<FString> SharedOwners;
		for (const auto& Binding : State.PrototypeNavigation.ClickBindings)
			for (const auto& Capture : Binding.Transition.SharedCaptures)
			{ SharedOwners.Add(Capture.NodeId); SharedOwners.Add(Capture.DestinationWidgetId); }
		bool bAddedOwner;
		do
		{
			bAddedOwner = false;
			for (auto& Binding : State.PrototypeNavigation.ClickBindings)
			{
				auto& Plan = Binding.Transition;
				if (!Plan.bFixedViewportLayout || !Plan.Diagnostic.IsEmpty()) continue;
				for (const auto& Layout : Plan.LayoutTargets)
				{
					if (Layout.DestinationWidgetId.IsEmpty() || (!SharedOwners.Contains(Layout.NodeId) && !SharedOwners.Contains(Layout.DestinationWidgetId))) continue;
					if (!SharedOwners.Contains(Layout.NodeId)) { SharedOwners.Add(Layout.NodeId); bAddedOwner = true; }
					if (!SharedOwners.Contains(Layout.DestinationWidgetId)) { SharedOwners.Add(Layout.DestinationWidgetId); bAddedOwner = true; }
					if (!Plan.SharedCaptures.ContainsByPredicate([&](const FUMGPrototypeSharedCapture& Capture) { return Capture.NodeId == Layout.NodeId; }))
						Plan.SharedCaptures.Add({Layout.NodeId, Layout.DestinationWidgetId});
				}
			}
		} while (bAddedOwner);
		for (const auto& Binding : State.PrototypeNavigation.ClickBindings)
		{
			for (const auto& Order : Binding.Transition.LayerOrders)
			{
				State.IndependentPaintParentIds.Add(Order.ParentNodeId);
				const auto* Target = Binding.Transition.LayoutTargets.FindByPredicate(
					[&Order](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Order.ParentNodeId; });
				if (Target) State.IndependentPaintParentIds.Add(Target->DestinationWidgetId);
			}
			for (const auto& Layer : Binding.Transition.AddedLayers)
				if (!Layer.bReplaceSource) State.AddedLayerPaintRanks.Add(Layer.NodeId, Layer.EndIndex);
		}
		TArray<TSharedPtr<FDesignNode>> PrototypeRoots = VisibleChildren;
		for (const auto& Host : State.PrototypeNavigation.StateHosts)
		{
			for (const auto& Entry : Host.States)
			{
				if (Entry.StateId == Host.InitialStateId) continue;
				TSharedPtr<FJsonObject> Source;
				FDesignDocument Parsed;
				if (!DesignJsonUtils::TryParseObject(Entry.SourceNodeJson, Source))
				{
					State.ConversionError = TEXT("cannot read reachable state source");
					break;
				}
				auto Envelope = MakeShared<FJsonObject>();
				Envelope->SetObjectField(TEXT("document"), Source);
				if (!FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Envelope), Parsed)
					|| Parsed.Children.Num() != 1 || !Parsed.Children[0])
				{
					State.ConversionError = TEXT("cannot parse reachable state source");
					break;
				}
				FFigmaParser::ApplyImageUrls(Parsed, Document.SourceDocument().ResolvedComponentImageUrls);
				auto Root = Parsed.Children[0];
				Design2UMGConverterPrivate::ScopeAddedSubtree(*Root, Host.InstanceWidgetId + TEXT("::"), Entry.WidgetId);
				Root->Transform.X = Root->Transform.Y = 0;
				StateSourceTrees.Add(Entry.WidgetId, Root);
				PrototypeRoots.Add(Root);
			}
		}
		Design2UMGConverterPrivate::AttachPrototypeLayers(PrototypeRoots, State.PrototypeNavigation, Document.SourceDocument().ResolvedComponentImageUrls, Result.Warnings);
		State.PrototypeTriggerNodeIds = PrototypeAnalysis.TriggerNodeIds;
		if (State.PrototypeNavigation.IsValid())
		{
			Result.MutablePrototypeNavigation() = State.PrototypeNavigation;
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

	for (auto& Host : State.PrototypeNavigation.StateHosts)
	{
		TArray<FUMGWidgetNode*> Pending;
		for (auto& Root : RenderWidgets) Pending.Add(&Root);
		FUMGWidgetNode* Original = nullptr;
		while (!Pending.IsEmpty())
		{
			auto* Node = Pending.Pop();
			if (Node->Id == Host.InstanceWidgetId)
			{
				if (Original) { Original = nullptr; break; }
				Original = Node;
			}
			for (auto& Child : Node->Children) Pending.Add(&Child);
		}
		if (!Original)
		{
			State.ConversionError = TEXT("state instance does not map uniquely to a widget");
			break;
		}
		FUMGWidgetNode Wrapper;
		Wrapper.Type = UMGWidgetTypes::Overlay;
		Wrapper.Id = Host.HostWidgetId;
		// The host takes the instance's place; states resolve by id, so it carries the design name.
		Wrapper.Name = Original->Name;
		Wrapper.Bounds = Original->Bounds;
		Wrapper.Slot = Original->Slot;
		Original->Bounds.X = Original->Bounds.Y = 0;
		Original->Slot = FUMGSlotData();
		for (auto& Entry : Host.States)
		{
			if (Entry.StateId == Host.InitialStateId)
			{
				Entry.Visibility = Original->WidgetStyle.Visibility.Get(TEXT("Visible"));
				Wrapper.Children.Add(MoveTemp(*Original));
				continue;
			}
			auto* StateRoot = StateSourceTrees.Find(Entry.WidgetId);
			if (!StateRoot || !StateRoot->IsValid())
			{
				State.ConversionError = TEXT("reachable state source is missing");
				break;
			}
			FUMGWidgetNode Widget = NodeOrchestrator.Convert(*StateRoot);
			if (Widget.Type.IsEmpty())
			{
				State.ConversionError = TEXT("cannot convert reachable state source");
				break;
			}
			Entry.Visibility = Widget.WidgetStyle.Visibility.Get(TEXT("Visible"));
			Widget.Slot = FUMGSlotData();
			Widget.WidgetStyle.Visibility = TEXT("Collapsed");
			Wrapper.Children.Add(MoveTemp(Widget));
		}
		if (!State.ConversionError.IsEmpty()) break;
		*Original = MoveTemp(Wrapper);
	}

	// A changed leaf has two real visual nodes; its surrounding layout remains unchanged.
	TSet<FString> ReplacedAnimationLayers;
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		auto& Transition = Binding.Transition;
		if (!Transition.Diagnostic.IsEmpty()) continue;
		TArray<TPair<FString, FUMGWidgetNode>> StagedReplacements;
		for (const auto& Layer : Transition.AddedLayers)
		{
			if (!Layer.bReplaceSource || ReplacedAnimationLayers.Contains(Layer.NodeId)) continue;
			TSharedPtr<FJsonObject> Source;
			FDesignDocument Parsed;
			if (!DesignJsonUtils::TryParseObject(Layer.SourceNodeJson, Source))
			{
				Transition.Diagnostic = TEXT("cannot read replacement layer source");
				break;
			}
			auto Envelope = MakeShared<FJsonObject>();
			Envelope->SetObjectField(TEXT("document"), Source);
			if (!FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Envelope), Parsed)
				|| Parsed.Children.Num() != 1 || !Parsed.Children[0])
			{
				Transition.Diagnostic = TEXT("cannot parse replacement layer source");
				break;
			}
			FFigmaParser::ApplyImageUrls(Parsed, Document.SourceDocument().ResolvedComponentImageUrls);
			auto Added = Parsed.Children[0];
			Design2UMGConverterPrivate::ScopeAddedSubtree(*Added, Transition.LayerScopeId() + TEXT("::"), Layer.NodeId);
			Added->Transform.X = Added->Transform.Y = 0;
			Added->Transform.Opacity = 0;
			FUMGWidgetNode Widget = NodeOrchestrator.Convert(Added);
			if (Widget.Type.IsEmpty())
			{
				Transition.Diagnostic = TEXT("cannot convert replacement layer source");
				break;
			}
			StagedReplacements.Emplace(Layer.ParentNodeId, MoveTemp(Widget));
		}
		if (Transition.Diagnostic.IsEmpty())
		{
			for (auto& Pair : StagedReplacements)
			{
				TArray<FUMGWidgetNode*> Pending;
				for (auto& Root : RenderWidgets) Pending.Add(&Root);
				FUMGWidgetNode* Original = nullptr;
				while (!Pending.IsEmpty())
				{
					auto* Node = Pending.Pop();
					if (Node->Id == Pair.Key)
					{
						if (Original) { Original = nullptr; break; }
						Original = Node;
					}
					for (auto& Child : Node->Children) Pending.Add(&Child);
				}
				if (!Original)
				{
					Transition.Diagnostic = TEXT("replacement layer source is not uniquely mapped");
					break;
				}
				FUMGWidgetNode Wrapper;
				Wrapper.Type = UMGWidgetTypes::Overlay;
				Wrapper.Id = Original->Id + TEXT("__transition");
				Wrapper.Name = Original->Name + TEXT("_Transition");
				Wrapper.Bounds = Original->Bounds;
				Wrapper.Slot = Original->Slot;
				Original->Bounds.X = Original->Bounds.Y = 0;
				Original->Slot = FUMGSlotData();
				Pair.Value.Slot = FUMGSlotData();
				Pair.Value.WidgetStyle.Visibility = TEXT("HitTestInvisible");
				Wrapper.Children.Add(MoveTemp(*Original));
				Wrapper.Children.Add(MoveTemp(Pair.Value));
				*Original = MoveTemp(Wrapper);
				for (const auto& Layer : Transition.AddedLayers)
					if (Layer.bReplaceSource && Layer.ParentNodeId == Pair.Key) ReplacedAnimationLayers.Add(Layer.NodeId);
			}
		}
		if (!Transition.Diagnostic.IsEmpty())
		{
			Transition.WidgetTargets.Reset();
			Transition.LayerOrders.Reset();
			Result.Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s': %s; action not executed"),
				*Binding.TriggerWidgetId, *Transition.Diagnostic));
		}
		if (Binding.Steps.Num() == 1) Binding.Steps[0].Transition = Transition;
	}
	const auto FindRenderWidgetById = [&RenderWidgets](const FString& Id) -> FUMGWidgetNode*
	{
		TArray<FUMGWidgetNode*> Pending;
		for (auto& Root : RenderWidgets) Pending.Add(&Root);
		FUMGWidgetNode* Found = nullptr;
		while (!Pending.IsEmpty())
		{
			auto* Node = Pending.Pop();
			if (Node->Id == Id) { if (Found) return nullptr; Found = Node; }
			for (auto& Child : Node->Children) Pending.Add(&Child);
		}
		return Found;
	};
	const auto NativeVisual = [](FUMGWidgetNode* Node)
	{
		if (Node && Node->Type == UMGWidgetTypes::Button && Node->Children.Num() == 1
			&& Node->Children[0].Id == Node->Id + Design2UMGGeneratedNameSuffixes::Content()) return &Node->Children[0];
		return Node;
	};
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		auto& Plan = Binding.Transition;
		if (!Plan.Diagnostic.IsEmpty()) continue;
		for (auto& Item : Plan.SharedCaptures)
		{
			auto* Source = NativeVisual(FindRenderWidgetById(Item.NodeId));
			auto* Destination = NativeVisual(FindRenderWidgetById(Item.DestinationWidgetId));
			auto* Capture = Source ? Design2UMGConverterPrivate::SharedCaptureNode(*Source) : nullptr;
			auto* Target = Destination ? Design2UMGConverterPrivate::SharedCaptureNode(*Destination) : nullptr;
			if (!Capture || !Target)
			{
				Plan.Diagnostic = FString::Printf(TEXT("shared capture has no complete native owner pair [source=%s visual=%s type=%s children=%d capture=%d; target=%s visual=%s type=%s children=%d capture=%d]"),
					*Item.NodeId, Source ? *Source->Id : TEXT("<missing-or-ambiguous>"), Source ? *Source->Type : TEXT("<none>"), Source ? Source->Children.Num() : 0, Capture != nullptr,
					*Item.DestinationWidgetId, Destination ? *Destination->Id : TEXT("<missing-or-ambiguous>"), Destination ? *Destination->Type : TEXT("<none>"), Destination ? Destination->Children.Num() : 0, Target != nullptr);
				break;
			}
			auto& Targets = Capture->Effect.SharedCapture->Targets;
			if (!Targets.ContainsByPredicate([&](const FUMGSharedCaptureEndpoint& Endpoint) { return Endpoint.DestinationWidgetId == Item.DestinationWidgetId; }))
			{
				auto Endpoint = Target->Effect.SharedCapture->Source;
				Endpoint.DestinationWidgetId = Item.DestinationWidgetId; Targets.Add(MoveTemp(Endpoint));
			}
			Item.CaptureWidgetId = Capture->Id;
			Item.ContentWidgetId = Design2UMGConverterPrivate::LogicalContent(*Source)->Id;
			if (auto* Shadow = Design2UMGConverterPrivate::SharedShadowNode(*Source)) Item.ShadowWidgetId = Shadow->Id;
		}
	}
	// Each converted endpoint owns its static source plus all actually referenced targets.
	for (auto& Root : RenderWidgets)
	{
		TFunction<void(FUMGWidgetNode&)> FinishCapture = [&](FUMGWidgetNode& Node)
		{
			if (auto* Capture = Design2UMGConverterPrivate::SharedCaptureNode(Node))
			{
				const auto& Data = Capture->Effect.SharedCapture.GetValue();
				const bool HasShadow = !Data.Source.BoxShadows.IsEmpty() || Data.Targets.ContainsByPredicate(
					[](const FUMGSharedCaptureEndpoint& Target) { return !Target.BoxShadows.IsEmpty(); });
				if (auto* Shadow = Design2UMGConverterPrivate::SharedShadowNode(Node))
				{
					if (HasShadow) Shadow->Effect.SharedCapture = Data;
					else
					{
						const FString ShadowId = Shadow->Id;
						Node.Children.RemoveAt(0);
						if (Node.SourceVisualChildIndex != INDEX_NONE) --Node.SourceVisualChildIndex;
						for (auto& Binding : State.PrototypeNavigation.ClickBindings) for (auto& Item : Binding.Transition.SharedCaptures)
							if (Item.ShadowWidgetId == ShadowId) Item.ShadowWidgetId.Reset();
					}
				}
			}
			for (auto& Child : Node.Children) FinishCapture(Child);
		};
		FinishCapture(Root);
	}
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		auto& Plan = Binding.Transition;
		if (!Plan.Diagnostic.IsEmpty()) continue;
		Plan.LocalEndpoints.RemoveAll([&](const FUMGPrototypeLocalEndpoint& Endpoint)
		{
			auto* Source = NativeVisual(FindRenderWidgetById(Endpoint.NodeId));
			auto* Destination = NativeVisual(FindRenderWidgetById(Endpoint.DestinationWidgetId));
			return Source && Destination && Source->SourceVisualChildIndex == INDEX_NONE && Destination->SourceVisualChildIndex == INDEX_NONE
				&& Design2UMGConverterPrivate::SharedCaptureNode(*Source)
				&& Design2UMGConverterPrivate::SharedCaptureNode(*Destination);
		});
		for (auto& Endpoint : Plan.LocalEndpoints)
		{
			auto* Source = NativeVisual(FindRenderWidgetById(Endpoint.NodeId));
			auto* Destination = NativeVisual(FindRenderWidgetById(Endpoint.DestinationWidgetId));
			if (!Source || !Destination)
			{ Plan.Diagnostic = TEXT("local visual endpoints have no unique native source and destination"); break; }
			FUMGWidgetNode Target;
			if (Endpoint.bText)
			{
				const auto& SourceGlyph = Source->SourceVisualChildIndex == INDEX_NONE ? *Source : Source->Children[Source->SourceVisualChildIndex];
				const auto& TargetGlyph = Destination->SourceVisualChildIndex == INDEX_NONE ? *Destination : Destination->Children[Destination->SourceVisualChildIndex];
				if ((SourceGlyph.Type != UMGWidgetTypes::TextBlock && !UMGTextPaintOwner::Find(SourceGlyph))
					|| (TargetGlyph.Type != UMGWidgetTypes::TextBlock && !UMGTextPaintOwner::Find(TargetGlyph)))
				{ Plan.Diagnostic = TEXT("local text endpoint requires one editable native TextBlock per face"); break; }
				if (Source->SourceVisualChildIndex == INDEX_NONE)
				{
					FUMGWidgetNode Glyph = MoveTemp(*Source), Wrapper;
					Wrapper.Type = UMGWidgetTypes::Overlay; Wrapper.Id = Glyph.Id; Wrapper.Name = Glyph.Name;
					Wrapper.Bounds = Glyph.Bounds; Wrapper.Slot = Glyph.Slot;
					Wrapper.Render = MoveTemp(Glyph.Render);
					if (Glyph.HasSourceData()) Wrapper.MutableSource() = Glyph.Source();
					Glyph.ClearSourceDesignIdentity(); Glyph.Id += TEXT("__local_glyph"); Glyph.Name += TEXT("__local_glyph");
					Glyph.Bounds.X = Glyph.Bounds.Y = 0; Glyph.Slot = FUMGSlotData();
					Glyph.Render = FUMGRenderStyleData();
					Wrapper.SourceVisualChildIndex = 0; Wrapper.Children.Add(MoveTemp(Glyph));
					*Source = MoveTemp(Wrapper);
				}
				Target = Destination->SourceVisualChildIndex == INDEX_NONE ? *Destination
					: Destination->Children[Destination->SourceVisualChildIndex];
			}
			else
			{
				if (!Source->Children.IsValidIndex(Source->SourceVisualChildIndex)
					|| !Destination->Children.IsValidIndex(Destination->SourceVisualChildIndex))
				{ Plan.Diagnostic = TEXT("local surface endpoints require one typed own-paint child per endpoint"); break; }
				Target = Destination->Children[Destination->SourceVisualChildIndex];
			}
			const auto MapLocalLayout = [&](const FString& From, const FString& To)
			{
				auto* Mapping = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == From; });
				if (!Mapping) Mapping = &Plan.LayoutTargets.Add_GetRef({From, To, FString()});
				else Mapping->DestinationWidgetId = To;
			};
			const FString SourceRoot = Plan.SourceStateWidgetId.IsEmpty() ? Binding.SourcePageId : Plan.SourceStateWidgetId;
			if (Source->Id != Endpoint.NodeId && Endpoint.NodeId != SourceRoot) MapLocalLayout(Source->Id, Destination->Id);
			if (!Endpoint.bText)
			{
				// The layout child is distinct from both own-paint children and retains the source children once.
				MapLocalLayout(Design2UMGConverterPrivate::LogicalContent(*Source)->Id, Design2UMGConverterPrivate::LogicalContent(*Destination)->Id);
			}
			Endpoint.SourceVisualWidgetId = Source->Children[Source->SourceVisualChildIndex].Id;
			Endpoint.TargetVisualWidgetId = Plan.LayerScopeId() + TEXT("::local::") + Endpoint.NodeId;
			TFunction<void(FUMGWidgetNode&, const FString&)> Identify = [&](FUMGWidgetNode& Node, const FString& Path)
			{
				Node.Id = Endpoint.TargetVisualWidgetId + Path; Node.Name = Node.Id; Node.ClearSourceDesignIdentity();
				for (int32 Index = 0; Index < Node.Children.Num(); ++Index) Identify(Node.Children[Index], Path + TEXT("_") + FString::FromInt(Index));
			};
			Identify(Target, FString());
			Target.Bounds.X = Target.Bounds.Y = 0; Target.Slot = FUMGSlotData();
			Target.Render = FUMGRenderStyleData(); Target.Render.RenderOpacity = 0;
			Target.WidgetStyle.Visibility = TEXT("Collapsed");
			if (!FindRenderWidgetById(Endpoint.TargetVisualWidgetId))
			{
				if (Endpoint.bText) Source->Children.Add(MoveTemp(Target));
				else Source->Children.Insert(MoveTemp(Target), Source->Children.Num() - 1);
			}
		}
		if (Binding.Steps.Num() == 1) Binding.Steps[0].Transition = Plan;
	}
	const auto FindTextRetainer = [&FindRenderWidgetById](const FString& Id) -> FUMGWidgetNode*
	{
		auto* Node = FindRenderWidgetById(Id);
		return Node ? UMGTextPaintOwner::Find(*Node) : nullptr;
	};
	const auto FindSurface = [&FindRenderWidgetById, &NativeVisual](const FString& Id) -> FUMGWidgetNode*
	{
		auto* Owner = NativeVisual(FindRenderWidgetById(Id));
		const FString ProducerOwnerId = Owner && Owner->Children.IsValidIndex(Owner->SourceVisualChildIndex)
			? Owner->Children[Owner->SourceVisualChildIndex].Id : Id;
		auto* Node = Owner ? FindRenderWidgetById(ProducerOwnerId + TEXT("__surface_paint")) : nullptr;
		return Node && Node->Type == UMGWidgetTypes::Image && Node->Effect.SurfacePaints.IsSet() ? Node : nullptr;
	};
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		for (const auto& Target : Binding.Transition.WidgetTargets)
		{
			if (Target.bSurfacePaintChange)
			{
				auto* Source = FindSurface(Target.NodeId);
				auto* Destination = FindSurface(Target.DestinationWidgetId);
				if (!Source || !Destination)
				{
					Binding.Transition.Diagnostic = TEXT("surface paint has no unique native Image producer");
					break;
				}
				if (!Source->Effect.SurfacePaintTargets.IsSet()) Source->Effect.SurfacePaintTargets = TArray<FUMGSurfacePaintTarget>();
				if (!Source->Effect.SurfacePaintTargets->ContainsByPredicate([&](const FUMGSurfacePaintTarget& Existing)
					{ return Existing.DestinationWidgetId == Target.DestinationWidgetId; }))
					Source->Effect.SurfacePaintTargets->Add({Target.DestinationWidgetId, Destination->Effect.SurfacePaints.GetValue()});
			}
			if (!Target.bTextPaintChange) continue;
			auto* Source = FindTextRetainer(Target.NodeId);
			auto* Destination = FindTextRetainer(Target.DestinationWidgetId);
			if (!Source || !Destination)
			{
				Binding.Transition.Diagnostic = TEXT("text paint owner is not uniquely mapped to a native retainer and TextBlock");
				Result.Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s': %s; action not executed"),
					*Binding.TriggerWidgetId, *Binding.Transition.Diagnostic));
				break;
			}
			if (!Source->Effect.TextPaintTargets.IsSet()) Source->Effect.TextPaintTargets = TArray<FUMGSurfacePaintTarget>();
			if (!Source->Effect.TextPaintTargets->ContainsByPredicate([&](const FUMGSurfacePaintTarget& Existing)
				{ return Existing.DestinationWidgetId == Target.DestinationWidgetId; }))
				Source->Effect.TextPaintTargets->Add({Target.DestinationWidgetId, Destination->Effect.TextPaints.GetValue()});
		}
		if (Binding.Steps.Num() == 1) Binding.Steps[0].Transition = Binding.Transition;
	}
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		for (const auto& Endpoint : Binding.Transition.LocalEndpoints)
		{
			if (!Endpoint.bText) continue;
			auto* Source = FindTextRetainer(Endpoint.NodeId);
			auto* Incoming = FindTextRetainer(Endpoint.TargetVisualWidgetId);
			if (Source && Incoming)
			{
				Incoming->Effect.TextPaints = Source->Effect.TextPaints;
				Incoming->Effect.TextPaintTargets = Source->Effect.TextPaintTargets;
			}
		}
	}
	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		auto& Plan = Binding.Transition;
		if (!Plan.bFixedViewportLayout || !Plan.Diagnostic.IsEmpty()) continue;
		const FString SourceId = Plan.SourceStateWidgetId.IsEmpty() ? Binding.SourcePageId : Plan.SourceStateWidgetId;
		// Each trigger owns its presentation sockets; paint endpoints remain shared by source/target identity.
		const FString Scope = Plan.LayerScopeId() + TEXT("::layout::") + Binding.TriggerType + TEXT("::") + Binding.TriggerWidgetId;
		TArray<FUMGWidgetNode*> Pending;
		for (auto& Root : RenderWidgets) Pending.Add(&Root);
		FUMGWidgetNode* Host = nullptr;
		while (!Pending.IsEmpty())
		{
			auto* Node = Pending.Pop();
			if (Node->Id == SourceId) { Host = Node; break; }
			for (auto& Child : Node->Children) Pending.Add(&Child);
		}
		if (!Host || Plan.LayerScopeId().IsEmpty())
		{
			Plan.Diagnostic = TEXT("fixed viewport layout has no source endpoint or transition scope");
			continue;
		}
		const FString VisualRootId = Host->Type == UMGWidgetTypes::Button && Host->Children.Num() == 1 ? Host->Children[0].Id : SourceId;
		if (Host->Type == UMGWidgetTypes::Button && Host->Children.Num() == 1) Host = &Host->Children[0];
		Plan.LayoutOriginalWidgetId = VisualRootId + TEXT("__layout_content");
		Plan.LayoutPresentationWidgetId = Scope + TEXT("__layout_presentation");
		Plan.LayoutSourceWidgetId = SourceId;
		Plan.LayoutDestinationWidgetId = Plan.TargetStateWidgetId.IsEmpty() ? Binding.Steps[0].TargetPageId : Plan.TargetStateWidgetId;
		Plan.LayoutSourcePageId = Binding.SourcePageId;
		const bool bPreparedHost = Host->Type == UMGWidgetTypes::Overlay && !Host->Children.IsEmpty()
			&& Host->Children[0].Id == Plan.LayoutOriginalWidgetId;
		if (!bPreparedHost)
		{
			FUMGWidgetNode Original = MoveTemp(*Host);
			FUMGWidgetNode Layer;
			Layer.Type = UMGWidgetTypes::Overlay;
			Layer.Id = Original.Id; Layer.Name = Original.Name;
			Layer.Bounds = Original.Bounds; Layer.Slot = Original.Slot;
			Layer.Render = MoveTemp(Original.Render); Layer.WidgetStyle = MoveTemp(Original.WidgetStyle);
			if (Original.HasSourceData()) Layer.MutableSource() = Original.Source();
			Original.ClearSourceDesignIdentity();
			Original.Id = Plan.LayoutOriginalWidgetId; Original.Name += TEXT("__layout_content");
			Original.Bounds.X = Original.Bounds.Y = 0; Original.Slot = FUMGSlotData();
			Original.Render = FUMGRenderStyleData(); Original.WidgetStyle = FUMGWidgetStyleData();
			Layer.Children.Add(MoveTemp(Original));
			*Host = MoveTemp(Layer);
		}
		TFunction<void(FUMGWidgetNode&, const FString&)> IdentifyLayoutNodes = [&](FUMGWidgetNode& Node, const FString& Path)
		{
			if (Node.Id.IsEmpty()) Node.Id = SourceId + TEXT("::__layout_node_") + Path;
			for (int32 Index = 0; Index < Node.Children.Num(); ++Index) IdentifyLayoutNodes(Node.Children[Index], Path + TEXT("_") + FString::FromInt(Index));
		};
		IdentifyLayoutNodes(Host->Children[0], TEXT("root"));
		FUMGWidgetNode Presentation = Host->Children[0];
		Presentation.Id = SourceId;
		TFunction<bool(const FUMGWidgetNode&)> ContainsTrigger = [&](const FUMGWidgetNode& Node)
		{
			if (Node.Id == Binding.TriggerWidgetId) return true;
			for (const auto& Child : Node.Children) if (ContainsTrigger(Child)) return true;
			return false;
		};
		TFunction<void(FUMGWidgetNode&)> PreparePresentation = [&](FUMGWidgetNode& Node)
		{
			if (const auto* Added = Plan.AddedLayers.FindByPredicate([&](const FUMGPrototypeAddedLayer& Layer)
				{ return Layer.bFlowChild && Layer.NodeId == Node.Id; }))
			{
				auto* Mapping = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Added->NodeId; });
				TFunction<void(FUMGWidgetNode&)> ScopeIncoming = [&](FUMGWidgetNode& Visual)
				{
					const FString OriginalId = Visual.Id;
					Visual.Id = Scope + TEXT("::") + OriginalId; Visual.Name = Scope + TEXT("_") + Visual.Name; Visual.ClearSourceDesignIdentity();
					for (auto& Live : Plan.LivePresentations) if (Live.PresentationSocketWidgetId == OriginalId) Live.PresentationSocketWidgetId = Visual.Id;
					for (auto& Child : Visual.Children) ScopeIncoming(Child);
				};
				ScopeIncoming(Node);
				Mapping->PresentationWidgetId = Node.Id;
				return;
			}
			if (Node.Effect.SharedCapture.IsSet())
			{
				const FString OriginalId = Node.Id;
				Node.Id = Scope + TEXT("::") + OriginalId; Node.Name = Scope + TEXT("_") + Node.Name; Node.ClearSourceDesignIdentity();
				for (auto& Item : Plan.SharedCaptures)
				{
					if (Item.CaptureWidgetId == OriginalId) Item.PresentationCaptureWidgetId = Node.Id;
					if (Item.ShadowWidgetId == OriginalId) Item.PresentationShadowWidgetId = Node.Id;
				}
				if (Node.Type == UMGWidgetTypes::RetainerBox)
				{
					auto& Canvas = Node.Children[0]; Canvas.Id = Scope + TEXT("::") + Canvas.Id; Canvas.Name = Scope + TEXT("_") + Canvas.Name;
					auto& Content = Canvas.Children[0]; const FString ContentId = Content.Id;
					PreparePresentation(Content);
					for (auto& Item : Plan.SharedCaptures) if (Item.ContentWidgetId == ContentId) Item.PresentationContentWidgetId = Content.Id;
				}
				return;
			}
			const auto* Local = Plan.LocalEndpoints.FindByPredicate([&](const FUMGPrototypeLocalEndpoint& Endpoint)
				{ return Endpoint.SourceVisualWidgetId == Node.Id || Endpoint.TargetVisualWidgetId == Node.Id; });
			if (Local)
			{
				const FString OriginalId = Node.Id;
				for (auto& Endpoint : Plan.LocalEndpoints)
				{
					if (Endpoint.SourceVisualWidgetId == OriginalId) Endpoint.SourcePresentationWidgetId = Scope + TEXT("::") + OriginalId;
					if (Endpoint.TargetVisualWidgetId == OriginalId) Endpoint.TargetPresentationWidgetId = Scope + TEXT("::") + OriginalId;
				}
				TFunction<void(FUMGWidgetNode&)> ScopeVisual = [&](FUMGWidgetNode& Visual)
				{ Visual.Id = Scope + TEXT("::") + Visual.Id; Visual.Name = Scope + TEXT("_") + Visual.Name;
					Visual.ClearSourceDesignIdentity(); for (auto& Child : Visual.Children) ScopeVisual(Child); };
				ScopeVisual(Node);
				return;
			}
			const bool bContainsTrigger = ContainsTrigger(Node);
			const FString OriginalId = Node.Id;
			Node.Id = Scope + TEXT("::") + OriginalId;
			Node.Name = Scope + TEXT("_") + Node.Name;
			Node.ClearSourceDesignIdentity();
			if (Node.Type == UMGWidgetTypes::HorizontalBox || Node.Type == UMGWidgetTypes::VerticalBox
				|| Node.Type == UMGWidgetTypes::WrapBox || Node.Type == UMGWidgetTypes::GridPanel || Node.Type == UMGWidgetTypes::UniformGridPanel)
				Node.Type = UMGWidgetTypes::CanvasPanel;
			auto* Mapping = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Target) { return Target.NodeId == OriginalId; });
			if (!Mapping) Mapping = &Plan.LayoutTargets.Add_GetRef({OriginalId, FString(), FString()});
			Mapping->PresentationWidgetId = Node.Id;
			const auto* StateHost = State.PrototypeNavigation.StateHosts.FindByPredicate(
				[&](const FUMGPrototypeStateHost& Item) { return Item.HostWidgetId == OriginalId; });
			const bool bTrigger = OriginalId != SourceId && OriginalId != Plan.LayoutDestinationWidgetId
				&& State.PrototypeNavigation.ClickBindings.ContainsByPredicate([&](const FUMGPrototypeNavigationBinding& Item)
				{ return Item.TriggerWidgetId == OriginalId && !Item.Steps.IsEmpty() && Item.Transition.Diagnostic.IsEmpty(); });
			if (!bContainsTrigger && (StateHost || bTrigger))
			{
				Plan.LivePresentations.Add({OriginalId, OriginalId + TEXT("__live_socket"), Node.Id, FString(), FString(), FString()});
				if (StateHost)
				{
					const auto* SourceLayout = Plan.LayoutTargets.FindByPredicate(
						[&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == StateHost->InstanceWidgetId; });
					if (SourceLayout)
					{
						const FString DestinationId = SourceLayout->DestinationWidgetId;
						const auto* DestinationHost = State.PrototypeNavigation.StateHosts.FindByPredicate(
							[&](const FUMGPrototypeStateHost& Item) { return Item.InstanceWidgetId == DestinationId; });
						Mapping->DestinationWidgetId = DestinationHost ? DestinationHost->HostWidgetId : DestinationId;
					}
				}
				TFunction<void(FUMGWidgetNode&)> MapLiveChildren = [&](FUMGWidgetNode& Child)
				{
					for (auto& Item : Plan.SharedCaptures)
					{
						if (Item.CaptureWidgetId == Child.Id) Item.PresentationCaptureWidgetId = Child.Id;
						if (Item.ShadowWidgetId == Child.Id) Item.PresentationShadowWidgetId = Child.Id;
						if (Item.ContentWidgetId == Child.Id) Item.PresentationContentWidgetId = Child.Id;
					}
					for (auto& Endpoint : Plan.LocalEndpoints)
					{
						if (Endpoint.SourceVisualWidgetId == Child.Id) Endpoint.SourcePresentationWidgetId = Child.Id;
						if (Endpoint.TargetVisualWidgetId == Child.Id) Endpoint.TargetPresentationWidgetId = Child.Id;
					}
					if (auto* Target = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Child.Id; }))
						Target->PresentationWidgetId = Child.Id;
					Child.Id = Scope + TEXT("::") + Child.Id;
					Child.ClearSourceDesignIdentity();
					for (auto& Descendant : Child.Children) MapLiveChildren(Descendant);
				};
				for (auto& Child : Node.Children) MapLiveChildren(Child);
				return;
			}
			for (auto& Child : Node.Children) PreparePresentation(Child);
		};
		for (const auto& Item : Plan.SharedCaptures)
		{
			auto* Destination = NativeVisual(FindRenderWidgetById(Item.DestinationWidgetId));
			if (Destination && !Plan.LayoutTargets.ContainsByPredicate([&](const FUMGPrototypeLayoutTarget& Target) { return Target.NodeId == Item.ContentWidgetId; }))
				Plan.LayoutTargets.Add({Item.ContentWidgetId, Design2UMGConverterPrivate::LogicalContent(*Destination)->Id, FString()});
		}
		PreparePresentation(Presentation);
		for (const auto& Layer : Plan.AddedLayers)
		{
			if (!Layer.bFlowChild) continue;
			TSharedPtr<FJsonObject> Raw;
			FDesignDocument Parsed;
			if (!DesignJsonUtils::TryParseObject(Layer.SourceNodeJson, Raw))
			{ Plan.Diagnostic = TEXT("incoming flow has no native destination source"); break; }
			auto Envelope = MakeShared<FJsonObject>(); Envelope->SetObjectField(TEXT("document"), Raw);
			if (!FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Envelope), Parsed) || Parsed.Children.Num() != 1)
			{ Plan.Diagnostic = TEXT("incoming flow destination source could not be parsed"); break; }
			FFigmaParser::ApplyImageUrls(Parsed, Document.SourceDocument().ResolvedComponentImageUrls);
			auto Added = RefExpander::DeepCopyNode(Parsed.Children[0]);
			Design2UMGConverterPrivate::ScopeAddedSubtree(*Added, Scope + TEXT("::"), Layer.NodeId);
			Design2UMGConverterPrivate::CollectLivePresentations(*Parsed.Children[0], *Added, State.PrototypeNavigation, Plan);
			Added->Transform.Opacity = 0; Added->LayoutData.bAbsoluteLayoutPositioning = true;
			auto Native = NodeOrchestrator.Convert(Added);
			if (Native.Type.IsEmpty()) { Plan.Diagnostic = TEXT("incoming flow has no native visual subtree"); break; }
			PreparePresentation(Native);
			const auto* ParentLayout = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Layer.ParentNodeId; });
			FUMGWidgetNode* Parent = nullptr;
			TFunction<void(FUMGWidgetNode&)> FindParent = [&](FUMGWidgetNode& Node)
			{
				if (ParentLayout && Node.Id == ParentLayout->PresentationWidgetId) Parent = &Node;
				for (auto& Child : Node.Children) FindParent(Child);
			};
			FindParent(Presentation);
			if (Parent && Design2UMGConverterPrivate::SharedCaptureNode(*Parent)) Parent = Design2UMGConverterPrivate::LogicalContent(*Parent);
			if (Parent && Parent->Type == UMGWidgetTypes::Border && Parent->Children.Num() == 1
				&& Parent->Children[0].Type == UMGWidgetTypes::CanvasPanel) Parent = &Parent->Children[0];
			if (!Parent || Parent->Type != UMGWidgetTypes::CanvasPanel)
			{ Plan.Diagnostic = TEXT("incoming flow has no fixed native presentation parent"); break; }
			Native.Slot.ZOrder = Layer.EndIndex; Parent->Children.Add(MoveTemp(Native));
		}
		Plan.LayoutTargets.RemoveAll([&](const FUMGPrototypeLayoutTarget& Target)
		{
			if (!Target.PresentationWidgetId.IsEmpty()) return false;
			const auto SourceNode = Design2UMGConverterPrivate::FindSourceNode(Document.Children, Target.NodeId);
			return SourceNode && SourceNode->Children.IsEmpty() && Design2UMGCore::DesignNodeSemantics::ShouldSkipNode(SourceNode);
		});
		Presentation.Bounds.X = Presentation.Bounds.Y = 0;
		Presentation.Slot = FUMGSlotData();
		FUMGWidgetNode Size;
		Size.Type = UMGWidgetTypes::SizeBox; Size.Id = Plan.LayoutPresentationWidgetId; Size.Name = Plan.LayoutPresentationWidgetId;
		Size.Bounds = Host->Bounds; Size.Bounds.X = Size.Bounds.Y = 0;
		Size.WidgetStyle.WidthOverride = Host->Bounds.W; Size.WidgetStyle.HeightOverride = Host->Bounds.H;
		Size.WidgetStyle.Visibility = TEXT("Collapsed");
		Size.Children.Add(MoveTemp(Presentation));
		if (!Host->Children.ContainsByPredicate([&](const FUMGWidgetNode& Child) { return Child.Id == Plan.LayoutPresentationWidgetId; }))
			Host->Children.Add(MoveTemp(Size));
	}

	for (auto& Binding : State.PrototypeNavigation.ClickBindings)
	{
		auto& Plan = Binding.Transition;
		for (const auto& Live : Plan.LivePresentations)
		{
			auto* Widget = FindRenderWidgetById(Live.WidgetId);
			auto* Presentation = FindRenderWidgetById(Live.PresentationSocketWidgetId);
			if (!Widget || !Presentation || Widget == Presentation)
			{ State.ConversionError = TEXT("live presentation requires unique original and presentation subtrees"); break; }
			TFunction<bool(const FUMGWidgetNode&)> ContainsPresentation = [&](const FUMGWidgetNode& Node)
			{
				if (Node.Id == Live.PresentationSocketWidgetId) return true;
				for (const auto& Child : Node.Children) if (ContainsPresentation(Child)) return true;
				return false;
			};
			if (ContainsPresentation(*Widget))
			{ State.ConversionError = TEXT("a live widget cannot be presented inside its own subtree"); break; }
			if (!FindRenderWidgetById(Live.OriginalSocketWidgetId))
			{
				FUMGWidgetNode Content = MoveTemp(*Widget);
				FUMGWidgetNode Socket;
				Socket.Type = UMGWidgetTypes::SizeBox; Socket.Id = Live.OriginalSocketWidgetId; Socket.Name = Socket.Id;
				Socket.Bounds = Content.Bounds; Socket.Slot = Content.Slot;
				Content.Bounds.X = Content.Bounds.Y = 0; Content.Slot = FUMGSlotData();
				Socket.Children.Add(MoveTemp(Content)); *Widget = MoveTemp(Socket);
			}
			// The socket owns only the outer animation envelope; native content keeps its own paint and opacity.
			FUMGWidgetNode Socket;
			Socket.Type = UMGWidgetTypes::SizeBox; Socket.Id = Live.PresentationSocketWidgetId; Socket.Name = Socket.Id;
			Socket.Bounds = Presentation->Bounds; Socket.Slot = Presentation->Slot;
			Socket.WidgetStyle.Visibility = TEXT("SelfHitTestInvisible");
			const bool bIncoming = Plan.WidgetTargets.ContainsByPredicate([&](const FUMGPrototypeWidgetAnimationTarget& Target)
				{ return Target.NodeId == Live.PresentationSocketWidgetId && !Target.bStartPresent && Target.bOpacity; });
			Socket.Render.RenderOpacity = bIncoming ? 0.0f : 1.0f;
			*Presentation = MoveTemp(Socket);
			for (auto& Target : Plan.WidgetTargets)
				if (Target.NodeId == Live.PresentationSocketWidgetId && !Target.bStartPresent && Target.bOpacity) Target.EndOpacity = 1.0f;
		}
		if (!State.ConversionError.IsEmpty()) break;
		TFunction<bool(FUMGWidgetNode&)> PrepareProxyInput = [&](FUMGWidgetNode& Node)
		{
			const bool bLive = Plan.LivePresentations.ContainsByPredicate(
				[&](const FUMGPrototypeLivePresentation& Item) { return Item.PresentationSocketWidgetId == Node.Id; });
			bool bLiveBelow = bLive;
			for (auto& Child : Node.Children) bLiveBelow |= PrepareProxyInput(Child);
			const FString Visibility = Node.WidgetStyle.Visibility.Get(TEXT("Visible"));
			if (Visibility != TEXT("Collapsed") && Visibility != TEXT("Hidden"))
				Node.WidgetStyle.Visibility = bLiveBelow ? TEXT("SelfHitTestInvisible") : TEXT("HitTestInvisible");
			return bLiveBelow;
		};
		for (const auto& Added : Plan.AddedLayers)
			if (auto* Node = FindRenderWidgetById(Added.NodeId)) PrepareProxyInput(*Node);
		if (!Plan.LivePresentations.IsEmpty() && !Plan.LayoutPresentationWidgetId.IsEmpty())
			if (auto* Node = FindRenderWidgetById(Plan.LayoutPresentationWidgetId)) PrepareProxyInput(*Node);
		if (Binding.Steps.Num() == 1) Binding.Steps[0].Transition = Plan;
	}
	if (State.PrototypeNavigation.IsValid()) Result.MutablePrototypeNavigation() = State.PrototypeNavigation;

	if (RenderWidgets.Num() == 1)
		Result.ExportBounds = ConvertedRootSource->Source().ExportBounds;

	if (State.PrototypeNavigation.IsValid())
	{
		if (RenderWidgets.Num() > 1)
		{
			const FString& InitialPageId = State.PrototypeNavigation.PageIds[State.PrototypeNavigation.InitialPageIndex];
			const auto* InitialRoot = VisibleChildren.FindByPredicate(
				[&InitialPageId](const TSharedPtr<FDesignNode>& Root) { return Root->Id == InitialPageId; });
			if (InitialRoot) Result.ExportBounds = (*InitialRoot)->Source().ExportBounds;
		}
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Convert: %d top-level widgets → prototype root with %d navigation bindings"),
			RenderWidgets.Num(),
			State.PrototypeNavigation.ClickBindings.Num());
		Result.RootWidget = Design2UMGPrototypeNavigation::BuildRoot(MoveTemp(RenderWidgets), State.PrototypeNavigation, bAddedPrototypePages);
		Result.MutablePrototypeNavigation() = State.PrototypeNavigation;
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
