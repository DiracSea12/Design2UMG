// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "WidgetBlueprint.h"

namespace DesignImportPipeline
{
	namespace
	{
		/** Find Target's parent within the Root subtree (compared by node pointer). */
		const FUMGWidgetNode* FindParentNode(const FUMGWidgetNode& Root, const FUMGWidgetNode& Target)
		{
			for (const FUMGWidgetNode& Child : Root.Children)
			{
				if (&Child == &Target)
				{
					return &Root;
				}
				if (const FUMGWidgetNode* Found = FindParentNode(Child, Target))
				{
					return Found;
				}
			}
			return nullptr;
		}
	}

	FDesign2UMGMappedInstanceRecord ManifestRecordToMappedRecord(
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record)
	{
		FDesign2UMGMappedInstanceRecord Mapped;
		Mapped.DesignNodeId = Record.DesignNodeId;
		Mapped.WidgetName = Record.WidgetName;
		Mapped.MappedKey = Record.MappedKey;
		Mapped.MappedClass = Record.MappedClass;
		Mapped.bMatchedAtSetLevel = Record.bMatchedAtSetLevel;
		Mapped.Inputs = Record.Inputs;
		return Mapped;
	}

	void ApplyRemovedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx)
	{
		// 9a) removed: safe-delete decision (delete only when purely D2U-owned; keep + conflict diagnostics when project content is present).
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Removed)
			{
				continue;
			}

			UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				// Already gone from the asset: only remove it from the new manifest records.
				Ctx.UpdatedD2UOwned->Remove(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_removed"),
					FString::Printf(
						TEXT("Design node '%s' (%s) removed from source; widget already absent from the tree"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			// Conservative gate 1 (design-side signal): the subtree holds instances alive this round -> no whole delete.
			// Common scenario: the design deletes the old container and moves its content into a new one; whole-deleting the
			// old container destroys the content instances, and reparent can never recover them afterwards. Keep the subtree
			// root so the instances take their own move plans and rehang under the new container (identity-preserving move).
			TArray<FString> LiveNames;
			if (CollectLiveInstancesInSubtree(
				Widget, *Ctx.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
					FString::Printf(
						TEXT("Design removed node '%s' (%s) preserved: its subtree still hosts live design instance(s) (%s); they keep their widgets and follow their own update plan"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*FString::Join(LiveNames, TEXT(", "))));
				continue;
			}

			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Ctx.WBP, Widget, *Ctx.OwnedNames, ConflictReason))
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
					FString::Printf(
						TEXT("Design removed node '%s' (%s) preserved: %s"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*ConflictReason));
				continue;
			}

			const TSet<FString> RemovedNames = DeleteD2UOwnedSubtreeSilently(Ctx.WBP, Widget, *Ctx.UpdatedD2UOwned);
			// Besides the subtree root, the diagnostic also names the other widget names inside the subtree: in the
			// single-page empty-root scenario the deleted manifest instance is a host synthetic layer (<root>_inner, no
			// source node id) - reporting only it would lose the identities of the actually deleted design nodes; widget
			// names derive deterministically from design node names and are a usable design identity outside the manifest.
			TArray<FString> SubtreeNames = RemovedNames.Array();
			SubtreeNames.Remove(Entry.ManifestRecord->WidgetName);
			SubtreeNames.Sort();
			const FString SubtreeSuffix = SubtreeNames.Num() > 0
				? FString::Printf(TEXT("; removed subtree widgets: %s"), *FString::Join(SubtreeNames, TEXT(", ")))
				: FString();
			AddReimportDiagnostic(
				*Ctx.Diagnostics, TEXT("reimport_removed"),
				FString::Printf(
					TEXT("Purely D2U-owned node '%s' (%s) removed through the safe delete flow%s"),
					*Entry.ManifestRecord->WidgetName,
					*Entry.ManifestRecord->DesignNodeId,
					*SubtreeSuffix));
		}
	}

	void ApplyKindChangedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx)
	{
		// 9b) kind-changed: the old subtree is replaced with the new form only when purely D2U-owned; otherwise kept as a conflict.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::KindChanged)
			{
				continue;
			}

			UWidget* OldWidget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!OldWidget)
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_node_missing"),
					FString::Printf(
						TEXT("Recorded widget '%s' (%s) not found; kind change left as-is"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Ctx.WBP, OldWidget, *Ctx.OwnedNames, ConflictReason))
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
					FString::Printf(
						TEXT("Kind change of '%s' (%s -> %s) preserved as-is: %s"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->Kind,
						Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated,
						*ConflictReason));
				continue;
			}

			UPanelWidget* ParentPanel = Cast<UPanelWidget>(OldWidget->GetParent());
			if (!ParentPanel)
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
					FString::Printf(
						TEXT("Kind change of '%s' skipped: parent is not a panel"),
						*Entry.ManifestRecord->WidgetName));
				continue;
			}

			// Conservative gate (design-side signal): the old subtree holds other instances alive this round -> no whole-delete replace.
			// A whole delete would destroy them, and the pruned new form would not rebuild them either -> the instances are truly lost.
			TArray<FString> LiveNames;
			if (CollectLiveInstancesInSubtree(
				OldWidget, *Ctx.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_kind_conflict"),
					FString::Printf(
						TEXT("Kind change of '%s' (%s -> %s) preserved as-is: old subtree still hosts live design instance(s) (%s)"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->Kind,
						Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated,
						*FString::Join(LiveNames, TEXT(", "))));
				continue;
			}

			DeleteD2UOwnedSubtreeSilently(Ctx.WBP, OldWidget, *Ctx.UpdatedD2UOwned);

			// The new-form subtree likewise prunes already-present manifest instances (rehung by reparent; not generated twice).
			FUMGWidgetNode PrunedNewForm = *Entry.NewNode;
			PruneManifestInstanceChildren(PrunedNewForm, *Ctx.InstancesById);
			UWidget* NewWidget = FUMGWidgetUtils::CreateWidgetRecursive(
				Ctx.WidgetTree, PrunedNewForm, ParentPanel, nullptr, nullptr, &Ctx.DesignSize, Ctx.MappingBuildState);
			if (NewWidget)
			{
				ApplySlotForNode(NewWidget, PrunedNewForm, Ctx);
				Ctx.NewlyCreatedWidgets->Add(Entry.ManifestRecord->DesignNodeId, NewWidget);
			}
			AddReimportDiagnostic(
				*Ctx.Diagnostics, TEXT("reimport_kind_changed"),
				FString::Printf(
					TEXT("Node '%s' (%s) transitioned %s -> %s; old D2U-owned subtree replaced with the new form"),
					*Entry.ManifestRecord->WidgetName,
					*Entry.ManifestRecord->DesignNodeId,
					*Entry.ManifestRecord->Kind,
					Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated));
		}
	}

	void ApplyAddedTopLevelNodes(
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx)
	{
		// 9c) added: new top-level purely generated nodes (including new mapped instances) hang under the host root
		//     first, so the later reparent can pair nodes the design moved into new containers under them.
		//     When the scope root itself is a manifest instance (the host-root form changed between imports, e.g. a
		//     synthesized host appears/disappears as interactions change), its children belong to that instance's nested
		//     refresh scope (handled by the 9e recursion) and must not hang under the host root as "host-root top-level additions".
		if (Ctx.HostPanel && !Ctx.InstancesById->Contains(GetDesignNodeId(PreparedScopeRoot)))
		{
			for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
			{
				if (Ctx.InstancesById->Contains(GetDesignNodeId(Child)))
				{
					continue;
				}

				// Prune manifest instances already inside the subtree (they are rehung by reparent, not generated twice).
				FUMGWidgetNode PrunedChild = Child;
				PruneManifestInstanceChildren(PrunedChild, *Ctx.InstancesById);
				UWidget* NewWidget = FUMGWidgetUtils::CreateWidgetRecursive(
					Ctx.WidgetTree, PrunedChild, Ctx.HostPanel, nullptr, nullptr, &Ctx.DesignSize, Ctx.MappingBuildState);
				if (NewWidget)
				{
					Ctx.NewlyCreatedWidgets->Add(GetDesignNodeId(Child), NewWidget);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_added"),
						FString::Printf(
							TEXT("New design node '%s' (%s) added under the page host root"),
							*Child.Name, *GetDesignNodeId(Child)));
				}
			}
		}
	}

	namespace
	{
		/** Result of the reparent target-parent resolution. */
		struct FReparentTargetResolution
		{
			UPanelWidget* TargetPanel = nullptr;
			bool bUnresolvable = false;
			bool bAlreadyInPlace = false;
		};

		/** The widget's current parent shares the base with the IR parent node (import naming rules) = the design never asked to move it. */
		bool WidgetParentMatchesNodeBase(const UWidget* Widget, const FUMGWidgetNode& ParentNode)
		{
			if (!Widget->GetParent())
			{
				return false;
			}
			const FString ParentBaseName = FUMGWidgetUtils::SanitizeWidgetName(
				ParentNode.Name, ParentNode.Type).ToString();
			return FUMGWidgetUtils::MatchesWidgetBaseName(
				Widget->GetParent()->GetFName().ToString(), ParentBaseName);
		}

		/**
		 * Reparent target-parent resolution (walk up from the IR direct parent):
		 * - wrapper nodes inserted by the converter (no source node id) are skipped outright;
		 * - a real design node must resolve onto a manifest instance or a widget created this round; otherwise the boundary
		 *   is unclear (bUnresolvable, conflict-kept); reaching the scope root = rehang under the host root;
		 * - current parent and IR parent sharing the base (synthetic-layer exemption / resolution-failure exemption share the check) = no move asked.
		 */
		FReparentTargetResolution ResolveReparentTarget(
			const FUMGWidgetNode& ScopeRoot,
			const FUMGWidgetNode& NewNode,
			UWidget* Widget,
			FReimportApplyContext& Ctx)
		{
			FReparentTargetResolution Resolution;
			const FUMGWidgetNode* ImmediateParentNode = FindParentNode(ScopeRoot, NewNode);
			const FUMGWidgetNode* AncestorNode = ImmediateParentNode;

			// A synthetic direct parent layer (inner etc., no source node id) pairs with the current parent by name: without
			// this check first, the target would resolve to the real node above the synthetic layer (typically a
			// single-content Border container) whose content slot the synthetic layer already occupies -> false move conflicts.
			if (ImmediateParentNode
				&& !HasRealSourceIdentity(*ImmediateParentNode)
				&& WidgetParentMatchesNodeBase(Widget, *ImmediateParentNode))
			{
				Resolution.bAlreadyInPlace = true;
				return Resolution;
			}

			while (AncestorNode && !Resolution.TargetPanel && !Resolution.bUnresolvable)
			{
				if (AncestorNode == &ScopeRoot)
				{
					Resolution.TargetPanel = Ctx.HostPanel;
					break;
				}

				const bool bRealDesignNode = HasRealSourceIdentity(*AncestorNode);
				UWidget* AncestorWidget = nullptr;
				if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ParentRecord =
					Ctx.InstancesById->Find(GetDesignNodeId(*AncestorNode)))
				{
					AncestorWidget = Ctx.WidgetTree->FindWidget(FName(*(*ParentRecord)->WidgetName));
				}
				else if (UWidget* const* NewlyCreated = Ctx.NewlyCreatedWidgets->Find(GetDesignNodeId(*AncestorNode)))
				{
					// The target parent is a purely generated node created this round: likewise clearly D2U-owned.
					AncestorWidget = *NewlyCreated;
				}

				if (AncestorWidget)
				{
					if (UPanelWidget* Panel = Cast<UPanelWidget>(AncestorWidget))
					{
						Resolution.TargetPanel = Panel;
					}
					// Resolved to a widget but not a panel (a content widget): keep walking up.
				}
				else if (bRealDesignNode)
				{
					// A real design node with unclear ownership: boundary unclear -> conflict-kept.
					Resolution.bUnresolvable = true;
				}
				AncestorNode = FindParentNode(ScopeRoot, *AncestorNode);
			}

			// No-conflict exception: resolution failed, but the widget's current parent is exactly the deterministic counterpart
			// of the IR direct parent (same-base-name hit) - reporting "reparent skipped" would contradict the actual behavior, so let it pass silently.
			if (Resolution.bUnresolvable
				&& ImmediateParentNode
				&& WidgetParentMatchesNodeBase(Widget, *ImmediateParentNode))
			{
				Resolution.bAlreadyInPlace = true;
			}
			return Resolution;
		}
	}

	void ApplyReparentPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx)
	{
		// 9d) reparent decision for matched (move/reparent only when both ends are clearly D2U-owned).
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Matched || !Ctx.HostPanel)
			{
				continue;
			}

			// The scope root itself is this instance (the host-root-form-change scenario): it is the new design's page
			// root, so "move elsewhere" has no meaning; skip reparent (9e refreshes its nested content as usual).
			if (Entry.NewNode == &PreparedScopeRoot)
			{
				continue;
			}

			UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				Ctx.KeepAsIsWidgetNames->Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_node_missing"),
					FString::Printf(
						TEXT("Recorded widget '%s' (%s) not found in the target tree; left as-is"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			const FReparentTargetResolution Resolution = ResolveReparentTarget(
				PreparedScopeRoot, *Entry.NewNode, Widget, Ctx);
			if (Resolution.bAlreadyInPlace)
			{
				continue;
			}
			if (!Resolution.TargetPanel || Resolution.bUnresolvable)
			{
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
					FString::Printf(
						TEXT("Reparent of '%s' skipped: new design parent is not clearly D2U-owned; structure preserved"),
						*Entry.ManifestRecord->WidgetName));
				continue;
			}
			UPanelWidget* TargetPanel = Resolution.TargetPanel;

			if (TargetPanel && Widget->GetParent() != TargetPanel)
			{
				// Source-side ownership check (the same rule as nested moves): the widget currently hangs under a
				// project-side container = the developer has taken over its placement; the design-side reparent must not drag it back - conflict-kept + diagnostic.
				FString SourceConflictReason;
				if (!IsMoveSourceOwned(Ctx, Widget, SourceConflictReason))
				{
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
						FString::Printf(
							TEXT("Reparent of '%s' skipped: %s; structure preserved"),
							*Entry.ManifestRecord->WidgetName, *SourceConflictReason));
					continue;
				}

				// The target is a single-content widget whose slot is already occupied (AddChild would reject it and create an
				// orphan): boundary unclear -> conflict-kept; the structure is untouched.
				const bool bTargetSingleContentOccupied =
					Cast<UContentWidget>(TargetPanel) && TargetPanel->GetChildrenCount() > 0;
				if (bTargetSingleContentOccupied)
				{
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
						FString::Printf(
							TEXT("Reparent of '%s' skipped: target '%s' is a single-content widget whose slot is already occupied; structure preserved"),
							*Entry.ManifestRecord->WidgetName,
							*TargetPanel->GetName()));
					continue;
				}

				if (UPanelWidget* OldPanel = Widget->GetParent())
				{
					OldPanel->Modify();
				}
				Widget->Modify();
				TargetPanel->Modify();
				Ctx.WidgetTree->RemoveWidget(Widget);
				if (TargetPanel->AddChild(Widget))
				{
					// After the move, drop the new slot first; the full refresh (properties/inputs/manifest record) is done
					// uniformly by 9e (this node is a Matched entry, so 9e finds it by widgetName for sure; refreshing here
					// too would write the mapped record twice into the new manifest).
					ApplySlotForNode(Widget, *Entry.NewNode, Ctx);
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_moved"),
						FString::Printf(
							TEXT("Widget '%s' reparented under '%s' (both endpoints D2U-owned); widget identity preserved"),
							*Entry.ManifestRecord->WidgetName,
							*TargetPanel->GetName()));
				}
				else
				{
					// Defensive: if it cannot be added back, rehang under the host root; never leave an orphan widget.
					if (Ctx.HostPanel)
					{
						Ctx.HostPanel->AddChild(Widget);
					}
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_move_conflict"),
						FString::Printf(
							TEXT("Reparent of '%s' to '%s' failed to attach; widget restored under the page host root"),
							*Entry.ManifestRecord->WidgetName,
							*TargetPanel->GetName()));
				}
			}
		}
	}

	TArray<UWidget*> CollectNestedMoveCandidates(
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx)
	{
		// 9d-2) nested-structure diff pre-scan: collect widgets "inside the owned list, not manifest instances, and no
		//       longer expected by the new design to stay in their original container" as the nested move-candidate pool.
		//       9e's container-subtree diff consumes move sources from the pool by deterministic same-name; unconsumed ones go to 9g's leftover-deletion diagnostics.
		TArray<UWidget*> NestedMoveCandidates;
		{
			TSet<FString> ExpectedNames;
			if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ScopeRootRecord =
				Ctx.InstancesById->Find(GetDesignNodeId(PreparedScopeRoot)))
			{
				// The scope root itself is an instance: the expected set starts from that instance widget's children.
				if (UPanelWidget* ScopeRootPanel = Cast<UPanelWidget>(
					Ctx.WidgetTree->FindWidget(FName(*(*ScopeRootRecord)->WidgetName))))
				{
					CollectExpectedNestedNames(PreparedScopeRoot, ScopeRootPanel, Ctx, ExpectedNames);
				}
			}
			else
			{
				for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
				{
					// Recurse into instance children only; non-instance children are subtrees newly created by 9c this round (brand new, no leftover semantics).
					if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ChildRecord =
						Ctx.InstancesById->Find(GetDesignNodeId(Child)))
					{
						if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(
							Ctx.WidgetTree->FindWidget(FName(*(*ChildRecord)->WidgetName))))
						{
							CollectExpectedNestedNames(Child, ChildPanel, Ctx, ExpectedNames);
						}
					}
				}
			}

			TSet<FString> InstanceWidgetNames;
			for (const TPair<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& Pair : *Ctx.InstancesById)
			{
				InstanceWidgetNames.Add(Pair.Value->WidgetName);
			}

			const TSet<FString>& CreatedThisRun = Ctx.MappingBuildState->GetCreatedWidgetNames();
			Ctx.WBP->WidgetTree->ForEachWidget([&](UWidget* TreeWidget)
			{
				const FString WidgetName = TreeWidget->GetFName().ToString();
				if (TreeWidget == Ctx.WBP->WidgetTree->RootWidget
					|| !Ctx.OwnedNames->Contains(WidgetName)
					|| InstanceWidgetNames.Contains(WidgetName)
					|| CreatedThisRun.Contains(WidgetName)
					|| ExpectedNames.Contains(WidgetName))
				{
					return;
				}
				NestedMoveCandidates.Add(TreeWidget);
			});
		}
		return NestedMoveCandidates;
	}

	void ApplyMatchedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
	{
		// 9e) matched: update D2U-owned properties/layout/slot/inputs; the Mapped Class change is intercepted here.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Matched)
			{
				continue;
			}

			UWidget* Widget = Ctx.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				// node_missing was already recorded in 9d and the manifest entry backfilled.
				continue;
			}

			// Mapped Class change = high-risk structural change: no replace, no update; conflict diagnostics (reporting old and new classes).
			if (Entry.bNewKindMapped && Entry.ManifestRecord->Kind == Design2UMGManifestKinds::Mapped)
			{
				const DesignComponentMapping::FComponentMappingEntry* NewEntry =
					GenerationSettings.ComponentMapping.Table.FindEntryByKey(Entry.NewNode->Source().MappedComponentKey);
				const FString NewMappedClass = NewEntry ? NewEntry->WidgetClass : FString();
				if (IsMappedClassConflict(Widget, Entry.ManifestRecord, NewMappedClass))
				{
					Ctx.UpdatedMappedRecords->Add(ManifestRecordToMappedRecord(*Entry.ManifestRecord));
					AddReimportDiagnostic(
						*Ctx.Diagnostics, TEXT("reimport_mapped_class_conflict"),
						FString::Printf(
							TEXT("Mapped class change on '%s' (%s): instance kept as '%s', manifest recorded '%s', mapping now points to '%s'; not replaced in v1"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->DesignNodeId,
							*Widget->GetClass()->GetPathName(),
							*Entry.ManifestRecord->MappedClass,
							*NewMappedClass));
					continue;
				}
			}

			RefreshNodeByKind(*Entry.NewNode, Widget, Ctx);
		}
	}

	void ReportNestedRemovalLeftovers(FReimportApplyContext& Ctx)
	{
		// 9g) nested-delete leftover diagnostics: owned subtrees in the candidate pool unconsumed by moves = nested nodes
		//     the new design no longer expects. With project content (project children / developer bindings) -> kept +
		//     conflict diagnostic naming them (spec R3 "Removed node contains project-side content" is not top-level-limited);
		//     purely D2U nested subtrees keep their current state with no diagnostic (nested deletion is outside the safe-delete scope; the accepted granularity).
		for (UWidget* Orphan : *Ctx.NestedMoveCandidates)
		{
			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Ctx.WBP, Orphan, *Ctx.OwnedNames, ConflictReason))
			{
				AddReimportDiagnostic(
					*Ctx.Diagnostics, TEXT("reimport_remove_conflict"),
					FString::Printf(
						TEXT("Design-removed nested node '%s' preserved: %s"),
						*Orphan->GetName(), *ConflictReason));
			}
		}
	}
}
