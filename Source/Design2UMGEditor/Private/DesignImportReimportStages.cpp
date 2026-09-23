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

	bool ApplyRemovedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames)
	{
		// Delete generated-only subtrees; retain project content and report conflicts.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Removed)
			{
				continue;
			}

			UWidget* Widget = Shared.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				// Already gone from the asset: only remove it from the new manifest records.
				UpdatedD2UOwned.Remove(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Shared.Diagnostics, ReimportDiagnosticKinds::Removed,
					FString::Printf(
						TEXT("Design node '%s' (%s) removed from source; widget already absent from the tree"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			// A subtree that still owns live instances in the new design cannot be deleted as a whole.
			// Common scenario: the design deletes the old container and moves its content into a new one; whole-deleting the
			// old container destroys the content instances, and reparent can never recover them afterwards. Keep the subtree
			// root so the instances take their own move plans and rehang under the new container (identity-preserving move).
			TArray<FString> LiveNames;
			if (CollectLiveInstancesInSubtree(
				Widget, *Shared.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::RemoveConflict,
					FString::Printf(
						TEXT("Design removed node '%s' (%s) preserved: its subtree still hosts live design instance(s) (%s); they keep their widgets and follow their own update plan"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*FString::Join(LiveNames, TEXT(", "))));
				continue;
			}

			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Shared.WBP, Widget, *Shared.OwnedNames, ConflictReason))
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::RemoveConflict,
					FString::Printf(
						TEXT("Design removed node '%s' (%s) preserved: %s"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->DesignNodeId,
						*ConflictReason));
				continue;
			}

			const TSet<FString> RemovedNames = DeleteD2UOwnedSubtreeSilently(Shared.WBP, Widget, UpdatedD2UOwned);
			bool bStillAttached = false;
			Shared.WidgetTree->ForEachWidget([&](UWidget* Current) { bStillAttached |= Current == Widget; });
			if (bStillAttached)
			{
				AddReimportDiagnostic(*Shared.Diagnostics, ReimportDiagnosticKinds::ApplyFailed,
					TEXT("Safe delete did not detach widget: ") + Entry.ManifestRecord->WidgetName);
				return false;
			}
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
				*Shared.Diagnostics, ReimportDiagnosticKinds::Removed,
				FString::Printf(
					TEXT("Purely D2U-owned node '%s' (%s) removed through the safe delete flow%s"),
					*Entry.ManifestRecord->WidgetName,
					*Entry.ManifestRecord->DesignNodeId,
					*SubtreeSuffix));
		}
		return true;
	}

	bool ApplyKindChangedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets)
	{
		// Replace a changed type only when the old subtree is entirely generated.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::KindChanged)
			{
				continue;
			}

			UWidget* OldWidget = Shared.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!OldWidget)
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Shared.Diagnostics, ReimportDiagnosticKinds::NodeMissing,
					FString::Printf(
						TEXT("Recorded widget '%s' (%s) not found; kind change left as-is"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Shared.WBP, OldWidget, *Shared.OwnedNames, ConflictReason))
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::KindConflict,
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
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::KindConflict,
					FString::Printf(
						TEXT("Kind change of '%s' skipped: parent is not a panel"),
						*Entry.ManifestRecord->WidgetName));
				continue;
			}

			// An old subtree that still owns live instances in the new design cannot be replaced by whole-subtree deletion.
			// A whole delete would destroy them, and the pruned new form would not rebuild them either -> the instances are truly lost.
			TArray<FString> LiveNames;
			if (CollectLiveInstancesInSubtree(
				OldWidget, *Shared.AliveInstanceWidgetNames, Entry.ManifestRecord->WidgetName, LiveNames))
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::KindConflict,
					FString::Printf(
						TEXT("Kind change of '%s' (%s -> %s) preserved as-is: old subtree still hosts live design instance(s) (%s)"),
						*Entry.ManifestRecord->WidgetName,
						*Entry.ManifestRecord->Kind,
						Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated,
						*FString::Join(LiveNames, TEXT(", "))));
				continue;
			}

			// Prepare and attach first; only a committed replacement may delete the old subtree.
			FUMGWidgetNode PrunedNewForm = *Entry.NewNode;
			PruneManifestInstanceChildren(PrunedNewForm, *Shared.InstancesById);
			UWidget* NewWidget = ReplaceReimportWidget(PrunedNewForm, OldWidget, Shared, UpdatedD2UOwned);
			if (!NewWidget) return false;
			NewlyCreatedWidgets.Add(Entry.ManifestRecord->DesignNodeId, NewWidget);
			AddReimportDiagnostic(
				*Shared.Diagnostics, ReimportDiagnosticKinds::KindChanged,
				FString::Printf(
					TEXT("Node '%s' (%s) transitioned %s -> %s; old D2U-owned subtree replaced with the new form"),
					*Entry.ManifestRecord->WidgetName,
					*Entry.ManifestRecord->DesignNodeId,
					*Entry.ManifestRecord->Kind,
					Entry.bNewKindMapped ? Design2UMGManifestKinds::Mapped : Design2UMGManifestKinds::Generated));
		}
		return true;
	}

	bool ApplyAddedTopLevelNodes(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TMap<FString, UWidget*>& NewlyCreatedWidgets)
	{
		// Attach generated top-level additions before reparenting resolves new containers. When the scope
		// root is a mapped instance, its children belong to nested refresh rather than host-root additions.
		if (Shared.HostPanel && !Shared.InstancesById->Contains(GetDesignNodeId(PreparedScopeRoot)))
		{
			for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
			{
				if (Shared.InstancesById->Contains(GetDesignNodeId(Child)))
				{
					continue;
				}

				// Prune manifest instances already inside the subtree (they are rehung by reparent, not generated twice).
				FUMGWidgetNode PrunedChild = Child;
				PruneManifestInstanceChildren(PrunedChild, *Shared.InstancesById);
				UWidget* NewWidget = CreateReimportWidget(PrunedChild, Shared.HostPanel, Shared);
				if (!NewWidget) return false;
				NewlyCreatedWidgets.Add(GetDesignNodeId(Child), NewWidget);
				AddReimportDiagnostic(
					*Shared.Diagnostics, ReimportDiagnosticKinds::Added,
					FString::Printf(
						TEXT("New design node '%s' (%s) added under the page host root"),
						*Child.Name, *GetDesignNodeId(Child)));
			}
		}
		return true;
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
		 * Resolve the reparent destination through design parents, skipping synthetic wrappers.
		 * Use the current manifest instance or scope host; unresolved ownership keeps a conflict.
		 * A matching parent base name requires no move.
		 */
		FReparentTargetResolution ResolveReparentTarget(
			const FUMGWidgetNode& ScopeRoot,
			const FUMGWidgetNode& NewNode,
			UWidget* Widget,
			const FReimportApplySharedState& Shared,
			const TMap<FString, UWidget*>& NewlyCreatedWidgets)
		{
			FReparentTargetResolution Resolution;
			const FUMGWidgetNode* ImmediateParentNode = Shared.TreeIndex->ParentsByNode.FindRef(&NewNode);
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
					Resolution.TargetPanel = Shared.HostPanel;
					break;
				}

				const bool bRealDesignNode = HasRealSourceIdentity(*AncestorNode);
				UWidget* AncestorWidget = nullptr;
				if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ParentRecord =
					Shared.InstancesById->Find(GetDesignNodeId(*AncestorNode)))
				{
					AncestorWidget = Shared.TreeIndex->WidgetsByName.FindRef(FName(*(*ParentRecord)->WidgetName));
				}
				else if (UWidget* const* NewlyCreated = NewlyCreatedWidgets.Find(GetDesignNodeId(*AncestorNode)))
				{
					// A target parent generated by this operation is clearly D2U-owned.
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
				AncestorNode = Shared.TreeIndex->ParentsByNode.FindRef(AncestorNode);
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

	bool ApplyReparentPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets)
	{
		// Reparent matched nodes only when both boundaries are generated-owned.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Matched || !Shared.HostPanel)
			{
				continue;
			}

			// A mapped scope root is the page root, not a reparent candidate; nested refresh handles its children.
			if (Entry.NewNode == &PreparedScopeRoot)
			{
				continue;
			}

			UWidget* Widget = Shared.TreeIndex->WidgetsByName.FindRef(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				KeepAsIsWidgetNames.Add(Entry.ManifestRecord->WidgetName);
				AddReimportDiagnostic(
					*Shared.Diagnostics, ReimportDiagnosticKinds::NodeMissing,
					FString::Printf(
						TEXT("Recorded widget '%s' (%s) not found in the target tree; left as-is"),
						*Entry.ManifestRecord->WidgetName, *Entry.ManifestRecord->DesignNodeId));
				continue;
			}

			const FReparentTargetResolution Resolution = ResolveReparentTarget(
				PreparedScopeRoot, *Entry.NewNode, Widget, Shared, NewlyCreatedWidgets);
			if (Resolution.bAlreadyInPlace)
			{
				continue;
			}
			if (!Resolution.TargetPanel || Resolution.bUnresolvable)
			{
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::MoveConflict,
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
				if (!IsMoveSourceOwned(Shared, Widget, SourceConflictReason))
				{
					AddReimportDegradation(
						Shared, ReimportDiagnosticKinds::MoveConflict,
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
					AddReimportDegradation(
						Shared, ReimportDiagnosticKinds::MoveConflict,
						FString::Printf(
							TEXT("Reparent of '%s' skipped: target '%s' is a single-content widget whose slot is already occupied; structure preserved"),
							*Entry.ManifestRecord->WidgetName,
							*TargetPanel->GetName()));
					continue;
				}

				if (!MoveReimportWidget(Widget, TargetPanel, *Entry.NewNode, Shared)) return false;
				AddReimportDiagnostic(
					*Shared.Diagnostics, ReimportDiagnosticKinds::Moved,
					FString::Printf(
						TEXT("Widget '%s' reparented under '%s' (both endpoints D2U-owned); widget identity preserved"),
						*Entry.ManifestRecord->WidgetName, *TargetPanel->GetName()));
			}
		}
		return true;
	}
	TArray<UWidget*> CollectNestedMoveCandidates(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared)
	{
		// Collect displaced owned non-instance widgets for same-name matching during nested refresh.
		// Unconsumed candidates identify nested removals.
		TArray<UWidget*> NestedMoveCandidates;
		{
			TSet<FString> ExpectedNames;
			if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ScopeRootRecord =
				Shared.InstancesById->Find(GetDesignNodeId(PreparedScopeRoot)))
			{
				// The scope root itself is an instance: the expected set starts from that instance widget's children.
				if (UPanelWidget* ScopeRootPanel = Cast<UPanelWidget>(
					Shared.TreeIndex->WidgetsByName.FindRef(FName(*(*ScopeRootRecord)->WidgetName))))
				{
					CollectExpectedNestedNames(PreparedScopeRoot, ScopeRootPanel, Shared, ExpectedNames);
				}
			}
			else
			{
				for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
				{
					// Recurse into instances only; newly added non-instance subtrees contain no previous widgets.
					if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* ChildRecord =
						Shared.InstancesById->Find(GetDesignNodeId(Child)))
					{
						if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(
							Shared.TreeIndex->WidgetsByName.FindRef(FName(*(*ChildRecord)->WidgetName))))
						{
							CollectExpectedNestedNames(Child, ChildPanel, Shared, ExpectedNames);
						}
					}
				}
			}

			TSet<FString> InstanceWidgetNames;
			for (const TPair<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& Pair : *Shared.InstancesById)
			{
				InstanceWidgetNames.Add(Pair.Value->WidgetName);
			}

			const TSet<FString>& CreatedThisRun = Shared.MappingBuildState->GetCreatedWidgetNames();
			Shared.WBP->WidgetTree->ForEachWidget([&](UWidget* TreeWidget)
			{
				const FString WidgetName = TreeWidget->GetFName().ToString();
				if (TreeWidget == Shared.WBP->WidgetTree->RootWidget
					|| !Shared.OwnedNames->Contains(WidgetName)
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

	bool ApplyMatchedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
		TArray<UWidget*>& NestedMoveCandidates,
		TMap<FString, UWidget*>& NewlyCreatedWidgets)
	{
		// Refresh generated properties, layout, slots and inputs after checking mapped-class conflicts.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Matched)
			{
				continue;
			}

			UWidget* Widget = Shared.WidgetTree->FindWidget(FName(*Entry.ManifestRecord->WidgetName));
			if (!Widget)
			{
				// Reparenting already recorded node_missing and retained the manifest entry.
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
					Shared.UpdatedMappedRecords->Add(ManifestRecordToMappedRecord(*Entry.ManifestRecord));
					AddReimportDegradation(
						Shared, ReimportDiagnosticKinds::MappedClassConflict,
						FString::Printf(
							TEXT("Mapped class change on '%s' (%s): instance kept as '%s', manifest recorded '%s', mapping now points to '%s'; automatic mapped-class replacement is not supported"),
							*Entry.ManifestRecord->WidgetName,
							*Entry.ManifestRecord->DesignNodeId,
							*Widget->GetClass()->GetPathName(),
							*Entry.ManifestRecord->MappedClass,
							*NewMappedClass));
					continue;
				}
			}

			if (!RefreshNodeByKind(*Entry.NewNode, Widget, Shared, NestedMoveCandidates, NewlyCreatedWidgets)) return false;
		}
		return true;
	}
	void ReportNestedRemovalLeftovers(
		const FReimportApplySharedState& Shared,
		const TArray<UWidget*>& NestedMoveCandidates)
	{
		// Retain unmatched nested subtrees. Report project-content conflicts explicitly;
		// report retained owned content as a partial synchronization.
		for (UWidget* Orphan : NestedMoveCandidates)
		{
			FString ConflictReason;
			if (!IsSubtreePurelyD2UOwned(Shared.WBP, Orphan, *Shared.OwnedNames, ConflictReason))
			{
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::RemoveConflict,
					FString::Printf(
						TEXT("Design-removed nested node '%s' preserved: %s"),
						*Orphan->GetName(), *ConflictReason));
				continue;
			}
			AddReimportDegradation(
				Shared, ReimportDiagnosticKinds::PartialSync,
				FString::Printf(
					TEXT("Design-removed nested D2U-owned node '%s' preserved because nested deletion is outside the safe-delete scope"),
					*Orphan->GetName()));
		}
	}
}
