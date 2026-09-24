// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPreviewContext.h"

#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/WidgetSwitcher.h"

namespace
{
	enum class EPreviewRuntimeRole : uint8
	{
		Button,
		Switcher,
		Overlay
	};

	using FAddressCounts = TMap<FString, int32>;

	struct FPreviewRuntimeAddressIndex
	{
		FAddressCounts Buttons;
		FAddressCounts Switchers;
		FAddressCounts Overlays;
	};

	template<typename NodeType>
	bool IndexNodes(
		NodeType& Node,
		TMap<FString, NodeType*>& Nodes,
		int32& AnonymousNodeCount,
		FString& Error)
	{
		if (!Node.Id.IsEmpty())
		{
			if (Nodes.Contains(Node.Id))
			{
				Error = FString::Printf(
					TEXT("Duplicate preview node Id '%s'. Each node must have a unique Id."),
					*Node.Id);
				return false;
			}
			Nodes.Add(Node.Id, &Node);
		}
		else if (Node.HasSourceData())
		{
			Error = TEXT("A source node has no stable preview Id; its identity cannot be matched after editing.");
			return false;
		}
		else
		{
			++AnonymousNodeCount;
		}

		for (auto& Child : Node.Children)
		{
			if (!IndexNodes(Child, Nodes, AnonymousNodeCount, Error))
			{
				return false;
			}
		}
		return true;
	}

	void AddGeneratedAddressAliases(const FString& Address, FAddressCounts& Counts)
	{
		++Counts.FindOrAdd(Address);

		int32 SeparatorIndex = INDEX_NONE;
		if (!Address.FindLastChar(TEXT('_'), SeparatorIndex) || SeparatorIndex <= 0)
		{
			return;
		}

		const FString CandidateBase = Address.Left(SeparatorIndex);
		if (FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Address, CandidateBase))
		{
			++Counts.FindOrAdd(CandidateBase);
		}
	}

	void IndexRuntimeAddresses(const FUMGWidgetNode& Node, FPreviewRuntimeAddressIndex& Index)
	{
		const FString Address = FUMGWidgetTreeBuilder::SanitizeWidgetName(Node.Name, Node.Type).ToString();
		if (UClass* WidgetClass = FUMGWidgetTreeBuilder::ResolveWidgetClass(Node.Type))
		{
			if (WidgetClass->IsChildOf(UButton::StaticClass()))
			{
				AddGeneratedAddressAliases(Address, Index.Buttons);
			}
			if (WidgetClass->IsChildOf(UWidgetSwitcher::StaticClass()))
			{
				AddGeneratedAddressAliases(Address, Index.Switchers);
			}
			if (WidgetClass->IsChildOf(UOverlay::StaticClass()))
			{
				AddGeneratedAddressAliases(Address, Index.Overlays);
			}
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			IndexRuntimeAddresses(Child, Index);
		}
	}

	const FAddressCounts& AddressesForRole(
		const FPreviewRuntimeAddressIndex& Index,
		const EPreviewRuntimeRole Role)
	{
		switch (Role)
		{
		case EPreviewRuntimeRole::Button:
			return Index.Buttons;
		case EPreviewRuntimeRole::Switcher:
			return Index.Switchers;
		case EPreviewRuntimeRole::Overlay:
			return Index.Overlays;
		default:
			checkNoEntry();
			return Index.Buttons;
		}
	}

	bool NodeHasRole(const FUMGWidgetNode& Node, const EPreviewRuntimeRole Role)
	{
		const UClass* WidgetClass = FUMGWidgetTreeBuilder::ResolveWidgetClass(Node.Type);
		if (!WidgetClass)
		{
			return false;
		}

		switch (Role)
		{
		case EPreviewRuntimeRole::Button:
			return WidgetClass->IsChildOf(UButton::StaticClass());
		case EPreviewRuntimeRole::Switcher:
			return WidgetClass->IsChildOf(UWidgetSwitcher::StaticClass());
		case EPreviewRuntimeRole::Overlay:
			return WidgetClass->IsChildOf(UOverlay::StaticClass());
		default:
			return false;
		}
	}

	void IndexPages(
		const FUMGWidgetNode& Node,
		const TSet<FString>& Pages,
		FString PageId,
		TMap<FString, FString>& NodePages)
	{
		if (Pages.Contains(Node.Id))
		{
			PageId = Node.Id;
		}
		if (!Node.Id.IsEmpty() && !PageId.IsEmpty())
		{
			NodePages.Add(Node.Id, PageId);
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			IndexPages(Child, Pages, PageId, NodePages);
		}
	}

	struct FPageHostIndex
	{
		TMap<FString, const FUMGWidgetNode*> ChildrenById;
		FAddressCounts AddressCounts;
	};

	FPageHostIndex BuildPageHostIndex(const FUMGWidgetNode* Host)
	{
		FPageHostIndex Result;
		if (!Host)
		{
			return Result;
		}

		for (const FUMGWidgetNode& Child : Host->Children)
		{
			if (!Child.Id.IsEmpty())
			{
				Result.ChildrenById.Add(Child.Id, &Child);
			}
			AddGeneratedAddressAliases(
				FUMGWidgetTreeBuilder::SanitizeWidgetName(Child.Name, Child.Type).ToString(),
				Result.AddressCounts);
		}
		return Result;
	}

	int32 AddressCount(const FAddressCounts& Counts, const FString& Address)
	{
		return Counts.FindRef(Address);
	}

	bool UsesRuntimeButtonLookup(const FUMGPrototypeNavigationBinding& Binding)
	{
		if (!Binding.UsesClickTrigger() && !Binding.UsesHoverTrigger() && !Binding.UsesPressTrigger())
		{
			return false;
		}
		if (Binding.Steps.Num() != 1)
		{
			return false;
		}

		switch (Binding.Steps[0].Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
		case EUMGPrototypeNavigationStepKind::OpenLink:
		case EUMGPrototypeNavigationStepKind::Overlay:
		case EUMGPrototypeNavigationStepKind::Back:
			return true;
		default:
			return false;
		}
	}

	bool ReconcileNavigation(
		const FUMGWidgetData& Original,
		FUMGWidgetData& Edited,
		const TMap<FString, const FUMGWidgetNode*>& OriginalNodes,
		const TMap<FString, FUMGWidgetNode*>& EditedNodes,
		const FPreviewRuntimeAddressIndex& RuntimeAddresses,
		FString& Error)
	{
		auto& Navigation = Edited.MutablePrototypeNavigation();
		Navigation = Original.PrototypeNavigation();

		auto RequireStableReference = [&](const FString& Id) -> FUMGWidgetNode*
		{
			const FUMGWidgetNode* const* Before = OriginalNodes.Find(Id);
			FUMGWidgetNode* const* After = EditedNodes.Find(Id);
			if (!Before || !After || (*Before)->Type != (*After)->Type)
			{
				Error = FString::Printf(
					TEXT("Interaction reference '%s' was removed or changed type. Keep its stable Id and type."),
					*Id);
				return nullptr;
			}
			return *After;
		};

		auto RequireRuntimeReference = [&RuntimeAddresses, &RequireStableReference, &Error](
			const FString& Id,
			const FString& RuntimeName,
			const TCHAR* FallbackType,
			const EPreviewRuntimeRole Role) -> FUMGWidgetNode*
		{
			FUMGWidgetNode* Node = RequireStableReference(Id);
			if (!Node)
			{
				return nullptr;
			}

			const FString Address = FUMGWidgetTreeBuilder::SanitizeWidgetName(RuntimeName, FallbackType).ToString();
			const FString NodeAddress = FUMGWidgetTreeBuilder::SanitizeWidgetName(Node->Name, Node->Type).ToString();
			if (!NodeHasRole(*Node, Role)
				|| !FUMGWidgetTreeBuilder::MatchesWidgetBaseName(NodeAddress, Address)
				|| AddressCount(AddressesForRole(RuntimeAddresses, Role), Address) != 1)
			{
				Error = FString::Printf(
					TEXT("Interaction reference '%s' is not uniquely addressable by its runtime widget role."),
					*Id);
				return nullptr;
			}
			return Node;
		};

		FUMGWidgetNode* Switcher = nullptr;
		FUMGWidgetNode* Overlay = nullptr;
		if (Navigation.HasOverlayBindings())
		{
			Overlay = RequireRuntimeReference(
				Navigation.OverlayHostWidgetId,
				Navigation.OverlayHostWidgetName,
				TEXT("Overlay"),
				EPreviewRuntimeRole::Overlay);
			if (!Overlay)
			{
				return false;
			}
		}
		if (Navigation.RequiresSwitcher())
		{
			Switcher = RequireRuntimeReference(
				Navigation.SwitcherWidgetId,
				Navigation.SwitcherWidgetName,
				TEXT("WidgetSwitcher"),
				EPreviewRuntimeRole::Switcher);
			if (!Switcher)
			{
				return false;
			}

			TArray<FString> Pages;
			for (const FUMGWidgetNode& Child : Switcher->Children)
			{
				if (Child.Id.IsEmpty())
				{
					Error = TEXT("An interactive page needs a stable Id.");
					return false;
				}
				Pages.Add(Child.Id);
			}

			const int32 ActiveIndex = Switcher->WidgetState.ActiveWidgetIndex.Get(0);
			if (!Pages.IsValidIndex(ActiveIndex))
			{
				Error = TEXT("The active interaction page index is outside the edited page list.");
				return false;
			}
			Navigation.InitialPageIndex = ActiveIndex;

			// Only current host children are pages. A former page nested inside another page
			// belongs to its containing page; overlay mirrors are not new logical pages.
			if (Overlay)
			{
				for (const FUMGWidgetNode& Child : Overlay->Children)
				{
					if (!Child.Id.IsEmpty()
						&& !Child.Id.EndsWith(Design2UMGPrototypeTokens::OverlayMirrorSuffix()))
					{
						Pages.AddUnique(Child.Id);
					}
				}
			}
			Navigation.PageIds = MoveTemp(Pages);
		}

		const FPageHostIndex SwitcherPages = BuildPageHostIndex(Switcher);
		const FPageHostIndex OverlayPages = BuildPageHostIndex(Overlay);
		TSet<FString> LogicalPages;
		TMap<FString, int32> LogicalPageIndexes;
		for (int32 PageIndex = 0; PageIndex < Navigation.PageIds.Num(); ++PageIndex)
		{
			const FString& PageId = Navigation.PageIds[PageIndex];
			LogicalPages.Add(PageId);
			LogicalPageIndexes.Add(PageId, PageIndex);
		}
		TMap<FString, FString> NodePages;
		IndexPages(Edited.RootWidget, LogicalPages, FString(), NodePages);

		auto ResolveTarget = [
			&OriginalNodes,
			&EditedNodes,
			&RequireStableReference,
			&SwitcherPages,
			&OverlayPages,
			&LogicalPageIndexes,
			&Error](const FString& Id, int32& Index, const bool bOverlay) -> bool
		{
			if (!RequireStableReference(Id))
			{
				return false;
			}

			const int32* PageIndex = LogicalPageIndexes.Find(Id);
			if (!PageIndex)
			{
				Error = FString::Printf(TEXT("Interaction target '%s' is no longer a logical page."), *Id);
				return false;
			}
			Index = *PageIndex;

			const FPageHostIndex& HostPages = bOverlay ? OverlayPages : SwitcherPages;
			const FString Address = FUMGWidgetTreeBuilder::SanitizeWidgetName(Id, TEXT("CanvasPanel")).ToString();
			const FString MirrorId = Id + Design2UMGPrototypeTokens::OverlayMirrorSuffix();
			const FString MirrorAddress = FUMGWidgetTreeBuilder::SanitizeWidgetName(
				MirrorId,
				TEXT("CanvasPanel")).ToString();

			const FUMGWidgetNode* const* BaseTargetNode = HostPages.ChildrenById.Find(Id);
			const bool bBaseTarget = BaseTargetNode
				&& FUMGWidgetTreeBuilder::MatchesWidgetBaseName(
					FUMGWidgetTreeBuilder::SanitizeWidgetName((*BaseTargetNode)->Name, (*BaseTargetNode)->Type).ToString(),
					Address)
				&& AddressCount(HostPages.AddressCounts, Address) == 1;
			const FUMGWidgetNode* const* MirrorTargetNode = HostPages.ChildrenById.Find(MirrorId);
			const bool bMirrorTarget = bOverlay
				&& !bBaseTarget
				&& MirrorTargetNode
				&& FUMGWidgetTreeBuilder::MatchesWidgetBaseName(
					FUMGWidgetTreeBuilder::SanitizeWidgetName((*MirrorTargetNode)->Name, (*MirrorTargetNode)->Type).ToString(),
					MirrorAddress)
				&& AddressCount(HostPages.AddressCounts, MirrorAddress) == 1;
			if (!bBaseTarget && !bMirrorTarget)
			{
				Error = FString::Printf(
					TEXT("Interaction target '%s' is not uniquely addressable under its page host."),
					*Id);
				return false;
			}

			if (bMirrorTarget && OriginalNodes.Contains(MirrorId))
			{
				FUMGWidgetNode* const* EditedMirror = EditedNodes.Find(MirrorId);
				const FUMGWidgetNode* const* OriginalMirror = OriginalNodes.Find(MirrorId);
				if (!EditedMirror || !OriginalMirror || (*EditedMirror)->Type != (*OriginalMirror)->Type)
				{
					Error = FString::Printf(
						TEXT("Interaction mirror reference '%s' was removed or changed type."),
						*MirrorId);
					return false;
				}
			}
			return true;
		};

		for (FUMGPrototypeNavigationBinding& Binding : Navigation.ClickBindings)
		{
			if (UsesRuntimeButtonLookup(Binding))
			{
				if (!RequireRuntimeReference(
					Binding.TriggerWidgetId,
					Binding.TriggerWidgetId,
					TEXT("Button"),
					EPreviewRuntimeRole::Button))
				{
					return false;
				}

				const FString MirrorId = Binding.TriggerWidgetId
					+ Design2UMGPrototypeTokens::OverlayMirrorSuffix();
				if (OriginalNodes.Contains(MirrorId)
					&& !RequireRuntimeReference(
						MirrorId,
						MirrorId,
						TEXT("Button"),
						EPreviewRuntimeRole::Button))
				{
					return false;
				}
			}
			else if (!RequireStableReference(Binding.TriggerWidgetId))
			{
				return false;
			}

			if (!Binding.SourcePageId.IsEmpty())
			{
				const FString* Page = NodePages.Find(Binding.TriggerWidgetId);
				if (!Page)
				{
					Error = FString::Printf(
						TEXT("Interaction trigger '%s' is no longer inside a page."),
						*Binding.TriggerWidgetId);
					return false;
				}
				Binding.SourcePageId = *Page;
			}

			if (!Binding.TargetPageId.IsEmpty()
				&& !ResolveTarget(
					Binding.TargetPageId,
					Binding.TargetPageIndex,
					Binding.Action == EUMGPrototypeNavigationAction::Overlay))
			{
				return false;
			}
			for (FUMGPrototypeNavigationStep& Step : Binding.Steps)
			{
				if (Step.RequiresPageTarget()
					&& !ResolveTarget(Step.TargetPageId, Step.TargetPageIndex, Step.IsOverlayAction()))
				{
					return false;
				}
			}
		}
		return true;
	}
}

bool RestoreDesignPreviewContext(
	const FUMGWidgetData& Original,
	FUMGWidgetData& Edited,
	FString& OutError)
{
	// These are per-refresh lookup tables, not persistent caches or additional source truth.
	TMap<FString, const FUMGWidgetNode*> OriginalNodes;
	TMap<FString, FUMGWidgetNode*> EditedNodes;
	int32 OriginalAnonymousNodeCount = 0;
	int32 EditedAnonymousNodeCount = 0;
	if (!IndexNodes(Original.RootWidget, OriginalNodes, OriginalAnonymousNodeCount, OutError)
		|| !IndexNodes(Edited.RootWidget, EditedNodes, EditedAnonymousNodeCount, OutError))
	{
		return false;
	}

	TArray<FString> MissingSourceIds;
	// Stable Id is the only source-identity join. A missing source Id can be an intentional
	// deletion; a surviving Id restores its sidecar without any name/position guess.
	for (const auto& Pair : OriginalNodes)
	{
		const FUMGWidgetNode* Before = Pair.Value;
		if (!Before->HasSourceData())
		{
			continue;
		}

		FUMGWidgetNode* const* After = EditedNodes.Find(Pair.Key);
		if (!After)
		{
			MissingSourceIds.Add(Pair.Key);
			continue;
		}
		if (Before->Type != (*After)->Type)
		{
			OutError = FString::Printf(
				TEXT("Source node '%s' changed widget type from '%s' to '%s'."),
				*Pair.Key,
				*Before->Type,
				*(*After)->Type);
			return false;
		}
		(*After)->MutableSource() = Before->Source();
	}

	if (!MissingSourceIds.IsEmpty())
	{
		bool bHasNewStableId = false;
		for (const auto& Pair : EditedNodes)
		{
			if (!OriginalNodes.Contains(Pair.Key))
			{
				bHasNewStableId = true;
				break;
			}
		}
		// Anonymous nodes have no stable identity to compare. When a source Id also disappears,
		// even a net-equal anonymous count cannot prove whether this was deletion or Id removal.
		const bool bHasAnonymousNode = EditedAnonymousNodeCount > 0;
		if (bHasNewStableId || bHasAnonymousNode)
		{
			MissingSourceIds.Sort();
			OutError = FString::Printf(
				TEXT("Preview edit is ambiguous: source Id(s) '%s' disappeared while new or Id-less nodes appeared. "
					"Keep the original Id on retained nodes. If deletion is intended, make this edit contain only that deletion, without new or re-identified nodes."),
				*FString::Join(MissingSourceIds, TEXT(", ")));
			return false;
		}
	}

	if (Original.HasSourceDocumentData())
	{
		Edited.MutableSourceDocument() = Original.SourceDocument();
	}
	Edited.Warnings = Original.Warnings;
	Edited.IconFontMappingChanges = Original.IconFontMappingChanges;

	if (!Original.HasPrototypeNavigationData())
	{
		return true;
	}

	FPreviewRuntimeAddressIndex RuntimeAddresses;
	IndexRuntimeAddresses(Edited.RootWidget, RuntimeAddresses);
	return ReconcileNavigation(
		Original,
		Edited,
		OriginalNodes,
		EditedNodes,
		RuntimeAddresses,
		OutError);
}
