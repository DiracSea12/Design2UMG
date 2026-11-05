// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigation.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "Design2UMGPrototypeStepGraph.h"
#include "Design2UMGWidgetNames.h"
#include "DesignSchemaSourceExtensions.h"
#include "RefExpander.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"

namespace
{
	/**
	 * Graph wiring resolves pages and trigger buttons by unique runtime base name. Each keeps its
	 * design name unless another widget of its role could answer the same lookup; then it uses its
	 * stable id. A contested page name stays with the initial page.
	 */
	void AssignAddressNames(TArray<FUMGWidgetNode>& PageWidgets, FUMGPrototypeNavigationData& NavigationData)
	{
		const auto UsesId = [](const FUMGWidgetNode& Node, const TArray<FUMGWidgetNode*>& Peers, const TCHAR* Role, bool bOwnsContestedName)
		{
			return !Node.Id.IsEmpty() && (Node.Name.IsEmpty() || (!bOwnsContestedName && Peers.ContainsByPredicate(
				[&](const FUMGWidgetNode* Peer) { return Peer != &Node && Design2UMGWidgetNames::Collide(Peer->Name, Node.Name, Role); })));
		};
		TArray<FUMGWidgetNode*> Pages;
		for (FUMGWidgetNode& Page : PageWidgets) Pages.Add(&Page);
		TArray<bool> PageUsesId;
		for (int32 Index = 0; Index < Pages.Num(); ++Index)
			PageUsesId.Add(UsesId(*Pages[Index], Pages, TEXT("CanvasPanel"), Index == NavigationData.InitialPageIndex));
		for (int32 Index = 0; Index < Pages.Num(); ++Index) if (PageUsesId[Index]) Pages[Index]->Name = Pages[Index]->Id;

		TSet<FString> TriggerIds;
		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings) TriggerIds.Add(Binding.TriggerWidgetId);
		TArray<FUMGWidgetNode*> Buttons;
		TFunction<void(FUMGWidgetNode&)> CollectButtons = [&](FUMGWidgetNode& Node)
		{
			if (Node.Type == UMGWidgetTypes::Button) Buttons.Add(&Node);
			for (FUMGWidgetNode& Child : Node.Children) CollectButtons(Child);
		};
		for (FUMGWidgetNode& Page : PageWidgets) CollectButtons(Page);
		TMap<FString, FString> TriggerNames;
		for (FUMGWidgetNode* Button : Buttons)
			if (TriggerIds.Contains(Button->Id))
				TriggerNames.Add(Button->Id, UsesId(*Button, Buttons, TEXT("Button"), false) ? Button->Id : Button->Name);
		for (FUMGWidgetNode* Button : Buttons)
			if (const FString* Name = TriggerNames.Find(Button->Id)) Button->Name = *Name;
		for (FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
			if (const FString* Name = TriggerNames.Find(Binding.TriggerWidgetId)) Binding.TriggerWidgetName = *Name;
	}

	void ResetPrototypePageForHost(FUMGWidgetNode& PageWidget)
	{
		PageWidget.Bounds.X = 0.0f;
		PageWidget.Bounds.Y = 0.0f;
		PageWidget.Slot = FUMGSlotData();
	}

	/** Hosted-page conventions for a page parented under the overlay host at build time. */
	void ApplyHostedPageConventions(FUMGWidgetNode& PageWidget)
	{
		PageWidget.Slot.Anchors.Max.X = 1.0f;
		PageWidget.Slot.Anchors.Max.Y = 1.0f;
		PageWidget.Slot.Offsets.Right = 0.0f;
		PageWidget.Slot.Offsets.Bottom = 0.0f;
		PageWidget.WidgetStyle.Visibility = TEXT("Collapsed");
	}

	/**
	 * Clone a page for the overlay host, clear design identity and append OverlayMirrorSuffix.
	 * Keep source metadata, including component mapping. The non-numeric suffix excludes
	 * mirrors from normal base-name matching while keeping them addressable by graph wiring.
	 */
	void MirrorWidgetSubtreeForOverlay(FUMGWidgetNode& Node)
	{
		Node.ClearSourceDesignIdentity();
		Node.Id += Design2UMGPrototypeTokens::OverlayMirrorSuffix();
		Node.Name += Design2UMGPrototypeTokens::OverlayMirrorSuffix();
		for (FUMGWidgetNode& Child : Node.Children)
		{
			MirrorWidgetSubtreeForOverlay(Child);
		}
	}

	/**
	 * Collect targets for one action kind from executable steps and binding summaries.
	 */
	TSet<FString> CollectActionTargetPageIds(
		const FUMGPrototypeNavigationData& NavigationData,
		EUMGPrototypeNavigationStepKind Kind)
	{
		const EUMGPrototypeNavigationAction BindingAction =
			Kind == EUMGPrototypeNavigationStepKind::Overlay
				? EUMGPrototypeNavigationAction::Overlay
				: EUMGPrototypeNavigationAction::Navigate;

		TSet<FString> Targets;
		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
		{
			if (Binding.Steps.Num() > 0)
			{
				for (const FUMGPrototypeNavigationStep& Step : Binding.Steps)
				{
					if (Step.Kind == Kind)
					{
						Targets.Add(Step.TargetPageId);
					}
				}
			}
			else if (Binding.Action == BindingAction)
			{
				Targets.Add(Binding.TargetPageId);
			}
		}
		return Targets;
	}
}

namespace Design2UMGPrototypeNavigation
{
	bool AddReachablePages(FDesignDocument& Document, TArray<FString>& Warnings)
	{
		const auto& ExportedPages = Document.SourceDocument().PrototypePages;
		if (ExportedPages.IsEmpty() || Document.Children.IsEmpty()) return false;

		FDesignDocument Candidate = Document;
		TSet<FString> Seen;
		TSet<FString> Reachable;
		for (const auto& Root : Document.Children)
		{
			if (!Root || Root->Id.IsEmpty()) continue;
			Seen.Add(Root->Id);
			Reachable.Add(Root->Id);
		}
		for (const auto& Page : ExportedPages)
		{
			if (!Page || Page->Id.IsEmpty()) continue;
			if (Seen.Contains(Page->Id))
			{
				Warnings.AddUnique(FString::Printf(TEXT("Duplicate prototype page '%s' ignored"), *Page->Id));
				continue;
			}
			Seen.Add(Page->Id);
			TSharedPtr<FDesignNode> PageCopy = RefExpander::DeepCopyNode(Page);
			PageCopy->bReusable = false;
			Candidate.Children.Add(MoveTemp(PageCopy));
		}

		TArray<FString> CandidateWarnings;
		const FPrototypeNavigationAnalysis Analysis = BuildAnalysis(Candidate, Candidate.Children, CandidateWarnings);
		for (const FString& Warning : CandidateWarnings) Warnings.AddUnique(Warning);
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (const auto& Binding : Analysis.Data.ClickBindings)
			{
				if (!Reachable.Contains(Binding.SourcePageId) || Binding.Steps.Num() != 1) continue;
				const auto Kind = Binding.Steps[0].Kind;
				const bool bNavigate = Kind == EUMGPrototypeNavigationStepKind::Navigate;
				const bool bOverlay = Kind == EUMGPrototypeNavigationStepKind::Overlay;
				if (!bNavigate && !bOverlay) continue;
				const bool bMouse = Binding.UsesClickTrigger() || Binding.UsesMouseEnterTrigger()
					|| Binding.UsesHoverTrigger() || Binding.UsesPressTrigger() || Binding.UsesDragTrigger();
				const bool bKeyboard = bNavigate && (Binding.UsesKeyDownTrigger() || Binding.UsesKeyUpTrigger());
				const bool bPageOpen = Binding.UsesPageOpenTrigger()
					&& (bOverlay || (bNavigate && !Binding.Transition.WidgetTargets.IsEmpty()));
				if (!bMouse && !bKeyboard && !bPageOpen) continue;
				const FString& Target = Binding.Steps[0].TargetPageId;
				if (!Target.IsEmpty() && !Reachable.Contains(Target))
				{
					Reachable.Add(Target);
					bChanged = true;
				}
			}
		}

		const int32 OriginalCount = Document.Children.Num();
		for (int32 Index = OriginalCount; Index < Candidate.Children.Num(); ++Index)
		{
			const auto& Page = Candidate.Children[Index];
			if (Reachable.Contains(Page->Id)) Document.Children.Add(Page);
		}
		if (Document.Children.Num() == OriginalCount) return false;
		for (const auto& Root : Document.Children)
		{
			if (Root && !Root->Id.IsEmpty())
			{
				Document.MutableSourceDocument().PrototypeStartNodeId = Root->Id;
				break;
			}
		}
		return true;
	}

	FPrototypeNavigationAnalysis BuildAnalysis(
		const FDesignDocument& Document,
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
		TArray<FString>& Warnings)
	{
		return Design2UMGPrototypeStepGraph::BuildAnalysis(
			Document,
			VisibleChildren,
			Warnings);
	}

	bool ShouldWrapNodeWithButton(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGPrototypeNavigationData& NavigationData,
		const TSet<FString>& TriggerNodeIds)
	{
		return Node.IsValid()
			&& NavigationData.IsValid()
			&& TriggerNodeIds.Contains(Node->Id);
	}

	FUMGWidgetNode WrapWithButton(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Button;
		Button.Type = UMGWidgetTypes::Button;
		Button.Id = Node->Id;
		// The button takes the node's place, so it carries the design name; BuildRoot settles contested names.
		Button.Name = !Node->Name.IsEmpty() ? Node->Name : Node->Id.IsEmpty() ? FString(TEXT("PrototypeButton")) : Node->Id;
		Button.Bounds = Widget.Bounds;
		Button.Slot = Widget.Slot;
		Button.WidgetStyle.BackgroundColor = FLinearColor::Transparent;
		Button.WidgetStyle.HoveredColor = FLinearColor::Transparent;
		Button.WidgetStyle.PressedColor = FLinearColor::Transparent;
		Button.WidgetStyle.Padding = FMargin(0.0f);

		Widget.Id = Node->Id + Design2UMGGeneratedNameSuffixes::Content();
		Widget.Name = (Node->Name.IsEmpty() ? Node->Id : Node->Name) + Design2UMGGeneratedNameSuffixes::Content();
		Widget.Bounds.X = 0.0f;
		Widget.Bounds.Y = 0.0f;
		Widget.Slot = FUMGSlotData();
		Button.Children.Add(MoveTemp(Widget));
		return Button;
	}

	FUMGWidgetNode BuildRoot(
		TArray<FUMGWidgetNode>&& PageWidgets,
		FUMGPrototypeNavigationData& NavigationData,
		bool bUseInitialPageSize)
	{
		// Flow pages remain native canvas children so transitions can display two real pages together.
		const TSet<FString> OverlayPageIds = CollectActionTargetPageIds(
			NavigationData, EUMGPrototypeNavigationStepKind::Overlay);
		const TSet<FString> NavigatePageIds = CollectActionTargetPageIds(
			NavigationData, EUMGPrototypeNavigationStepKind::Navigate);

		FUMGWidgetNode PageHost;
		PageHost.Type = UMGWidgetTypes::CanvasPanel;
		PageHost.Id = NavigationData.PageHostWidgetId;
		PageHost.Name = NavigationData.PageHostWidgetName;

		float MaxWidth = 0.0f;
		float MaxHeight = 0.0f;
		int32 PageChildCount = 0;
		TArray<FUMGWidgetNode> OverlayPages;
		NavigationData.PageDisplayVisibility.Reset();
		const int32 InitialPagePosition = NavigationData.InitialPageIndex;
		AssignAddressNames(PageWidgets, NavigationData);
		NavigationData.PageWidgetNames.Reset();
		for (int32 PageIndex = 0; PageIndex < PageWidgets.Num(); ++PageIndex)
		{
			FUMGWidgetNode& PageWidget = PageWidgets[PageIndex];
			ResetPrototypePageForHost(PageWidget);
			NavigationData.PageWidgetNames.Add(PageWidget.Id, PageWidget.Name);
			// Exported destination pages preserve the selected page's initial canvas size.
			if (!bUseInitialPageSize || PageIndex == InitialPagePosition)
			{
				MaxWidth = FMath::Max(MaxWidth, PageWidget.Bounds.W);
				MaxHeight = FMath::Max(MaxHeight, PageWidget.Bounds.H);
			}

			const bool bOverlayTargetPage = OverlayPageIds.Contains(PageWidget.Id);
			const bool bOverlayOnlyPage = bOverlayTargetPage
				&& !NavigatePageIds.Contains(PageWidget.Id)
				&& PageIndex != InitialPagePosition;
			if (bOverlayOnlyPage)
			{
				ApplyHostedPageConventions(PageWidget);
				OverlayPages.Add(MoveTemp(PageWidget));
				continue;
			}
			if (PageIndex == InitialPagePosition)
			{
				NavigationData.InitialPageIndex = PageChildCount;
			}
			++PageChildCount;
			if (bOverlayTargetPage)
			{
				FUMGWidgetNode Mirror = PageWidget; // deep copy; children are value types
				ApplyHostedPageConventions(Mirror);
				MirrorWidgetSubtreeForOverlay(Mirror);
				OverlayPages.Add(MoveTemp(Mirror));
			}
			NavigationData.PageDisplayVisibility.Add(PageWidget.WidgetStyle.Visibility.Get(TEXT("Visible")));
			if (PageIndex != InitialPagePosition) PageWidget.WidgetStyle.Visibility = TEXT("Collapsed");
			PageWidget.Slot.Anchors.Max.X = 1.0f;
			PageWidget.Slot.Anchors.Max.Y = 1.0f;
			PageWidget.Slot.Offsets.Right = 0.0f;
			PageWidget.Slot.Offsets.Bottom = 0.0f;
			PageHost.Children.Add(MoveTemp(PageWidget));
		}

		PageHost.Bounds.W = MaxWidth;
		PageHost.Bounds.H = MaxHeight;
		if (!NavigationData.HasOverlayBindings())
		{
			return PageHost;
		}

		FUMGWidgetNode OverlayHost;
		OverlayHost.Type = UMGWidgetTypes::CanvasPanel;
		OverlayHost.Id = NavigationData.OverlayHostWidgetId;
		OverlayHost.Name = NavigationData.OverlayHostWidgetName;
		OverlayHost.Bounds = PageHost.Bounds;
		OverlayHost.Slot = FUMGSlotData();
		OverlayHost.WidgetStyle.Visibility = TEXT("Collapsed");
		OverlayHost.Children = MoveTemp(OverlayPages);

		PageHost.Bounds.X = 0.0f;
		PageHost.Bounds.Y = 0.0f;
		PageHost.Slot = FUMGSlotData();
		PageHost.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		PageHost.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::Overlay;
		Root.Id = NavigationData.PageHostWidgetId.IsEmpty()
			? TEXT("__d2u_prototype_root")
			: NavigationData.PageHostWidgetId + Design2UMGPrototypeTokens::OverlayRootSuffix();
		Root.Name = NavigationData.PageHostWidgetName.IsEmpty()
			? Root.Id
			: NavigationData.PageHostWidgetName + Design2UMGPrototypeTokens::OverlayRootSuffix();
		Root.Bounds = PageHost.Bounds;
		Root.Children.Add(MoveTemp(PageHost));
		Root.Children.Add(MoveTemp(OverlayHost));
		return Root;
	}
}
