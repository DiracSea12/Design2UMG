// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "Design2UMGSettings.h"
#include "Design2UMGComponentBlueprints.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGValueParsing.h"
#include "UMGWidgetUtils.h"
#include "WidgetLayoutAdapter.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

namespace DesignImportPipeline
{
	namespace
	{
		bool IsMappedCandidateNode(const FUMGWidgetNode& Node)
		{
			return Node.HasSourceData() && !Node.Source().MappedComponentKey.IsEmpty();
		}
	}

	// ---------------------------------------------------------------------------
	// Apply stage
	// ---------------------------------------------------------------------------

	void ApplySlotForNode(UWidget* Widget, const FUMGWidgetNode& Node, const FReimportApplySharedState& Shared)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}
		if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(Widget->GetParent()))
		{
			ParentPanel->Modify();
			Widget->Slot->Modify();
			const FUMGSlotData SlotData = FWidgetLayoutAdapter::Resolve(
				Node, ParentPanel, Shared.WidgetTree, Shared.DesignSize, false);
			FUMGWidgetUtils::ApplySlotProperties(Widget->Slot, SlotData);
		}
	}

	/** A mapped instance conflicts when its live class or configured target differs from the manifest class.
 * A missing mapping compares as an empty class path. */
	bool IsMappedClassConflict(
		const UWidget* Instance,
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record,
		const FString& CurrentMappedClass)
	{
		return Instance->GetClass()->GetPathName() != Record->MappedClass
			|| CurrentMappedClass != Record->MappedClass;
	}

	namespace
	{
		/**
		 * The mapped instance's own visual properties (visibility/opacity) are written with the generic properties at
		 * import, but reimport does not refresh them per the "mapped interior belongs to the project" boundary.
		 * Divergences between the declared design value and the instance's current state are named in a report; properties already overwritten by an explicit mapping input are skipped - the declared value is that property's true design intent, and it is already refreshed.
		 */
		void ReportMappedOwnAttrDivergence(
			const UUserWidget* Instance,
			const FUMGWidgetNode& Node,
			const TArray<FDesign2UMGMappedInputRecord>& InputRecords,
			const FReimportApplySharedState& Shared)
		{
			const auto bInputTargets = [&InputRecords](const TCHAR* PropertyName)
			{
				return InputRecords.ContainsByPredicate(
					[&PropertyName](const FDesign2UMGMappedInputRecord& Record)
					{
						return Record.Target == PropertyName;
					});
			};

			TArray<FString> Divergences;
			if (Node.WidgetStyle.Visibility.IsSet() && !bInputTargets(TEXT("Visibility")))
			{
				const ESlateVisibility DesignVisibility =
					FUMGValueParser::ParseVisibility(Node.WidgetStyle.Visibility.GetValue());
				if (Instance->GetVisibility() != DesignVisibility)
				{
					const UEnum* const VisibilityEnum = StaticEnum<ESlateVisibility>();
					Divergences.Add(FString::Printf(
						TEXT("visibility project='%s' design='%s'"),
						*VisibilityEnum->GetNameStringByValue(static_cast<int64>(Instance->GetVisibility())),
						*VisibilityEnum->GetNameStringByValue(static_cast<int64>(DesignVisibility))));
				}
			}
			if (Node.Render.RenderOpacity.IsSet() && !bInputTargets(TEXT("RenderOpacity"))
				&& !FMath::IsNearlyEqual(Instance->GetRenderOpacity(), Node.Render.RenderOpacity.GetValue()))
			{
				Divergences.Add(FString::Printf(
					TEXT("renderOpacity project=%.3f design=%.3f"),
					Instance->GetRenderOpacity(), Node.Render.RenderOpacity.GetValue()));
			}

			if (Divergences.Num() > 0)
			{
				AddReimportDegradation(Shared, ReimportDiagnosticKinds::MappedAttrKept,
					FString::Printf(
						TEXT("Mapped instance '%s' (%s) own visual attributes kept as project-side, not refreshed: %s"),
						*Instance->GetName(), *GetDesignNodeId(Node), *FString::Join(Divergences, TEXT("; "))));
			}
		}

		void RefreshMappedInstance(
			UUserWidget* Instance,
			const FUMGWidgetNode& Node,
			const FReimportApplySharedState& Shared)
		{
			// Walk-side guard for the Mapped Class change conflict: if the manifest-recorded mapped instance's class
			// mismatches the record, or the mapping now points at another class, do not refresh (the 4.4 conflict
			// semantics are owned by the plan stage's reimport_mapped_class_conflict diagnostic; staying silent here avoids duplicate diagnostics).
			if (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* RecordPtr =
				Shared.InstancesById->Find(GetDesignNodeId(Node)))
			{
				const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record = *RecordPtr;
				const DesignComponentMapping::FComponentMappingEntry* CurrentEntry =
					Shared.MappingBuildState->FindEntryForKey(Node.Source().MappedComponentKey);
				const FString CurrentMappedClass = CurrentEntry ? CurrentEntry->WidgetClass : FString();
				if (Record->Kind == Design2UMGManifestKinds::Mapped
					&& IsMappedClassConflict(Instance, Record, CurrentMappedClass))
				{
					return;
				}
			}

			// Both the update plan and parent refresh can reach a mapped instance. Deduplicate before
			// applying slots or inputs so each instance produces one refresh and one set of diagnostics.
			const FString ProspectiveDesignNodeId = GetDesignNodeId(Node);
			const FString ProspectiveWidgetName = Instance->GetFName().ToString();
			if (Shared.UpdatedMappedRecords->ContainsByPredicate(
				[&ProspectiveDesignNodeId, &ProspectiveWidgetName](const FDesign2UMGMappedInstanceRecord& Existing)
				{
					return Existing.DesignNodeId == ProspectiveDesignNodeId
						&& Existing.WidgetName == ProspectiveWidgetName;
				}))
			{
				return;
			}

			// mapped instances update only the outer layout + explicit inputs (direct overwrite); the interior is untouched.
			Instance->Modify();
			ApplySlotForNode(Instance, Node, Shared);

			const DesignComponentMapping::FComponentMappingEntry* Entry =
				Shared.MappingBuildState->FindEntryForKey(Node.Source().MappedComponentKey);
			if (!Entry)
			{
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::InputSkipped,
					FString::Printf(
						TEXT("Mapping entry for key '%s' disappeared on reimport; inputs of '%s' left as-is"),
						*Node.Source().MappedComponentKey,
						*Instance->GetName()));
				return;
			}

			TArray<FDesign2UMGMappedInputRecord> InputRecords;
			if (!Design2UMGComponentMappingGeneration::ApplyMappedComponentInputsToInstance(
				Instance, Node, *Entry, &InputRecords, *Shared.Diagnostics))
			{
				*Shared.bHasDegradation = true;
			}

			ReportMappedOwnAttrDivergence(Instance, Node, InputRecords, Shared);

			FDesign2UMGMappedInstanceRecord Record;
			Record.DesignNodeId = ProspectiveDesignNodeId;
			Record.WidgetName = ProspectiveWidgetName;
			Record.MappedKey = Entry->Key;
			Record.MappedClass = Entry->WidgetClass;
			Record.bMatchedAtSetLevel = Shared.MappingBuildState->IsMappedAtSetLevel(Node.Id);
			Record.Inputs = MoveTemp(InputRecords);

			Shared.UpdatedMappedRecords->Add(MoveTemp(Record));

			AddReimportDiagnostic(
				*Shared.Diagnostics, ReimportDiagnosticKinds::Updated,
				FString::Printf(
					TEXT("Mapped instance '%s' (%s) refreshed: outer layout + declared inputs only; internals untouched"),
					*ProspectiveWidgetName, *ProspectiveDesignNodeId));
		}
	}

	/**
	 * A move requires both the source parent and the entire moved subtree to be D2U-owned.
	 * Project-owned structure or bindings keep the subtree in place.
	 */
	bool IsMoveSourceOwned(const FReimportApplySharedState& Shared, UWidget* MoveSource, FString& OutReason)
	{
		UPanelWidget* SourceParent = MoveSource->GetParent();
		if (!SourceParent
			|| (SourceParent != Shared.HostPanel
				&& !Shared.OwnedNames->Contains(SourceParent->GetFName().ToString())))
		{
			OutReason = FString::Printf(
				TEXT("source sits under project-side container '%s'"),
				SourceParent ? *SourceParent->GetName() : TEXT("(none)"));
			return false;
		}
		return IsSubtreePurelyD2UOwned(Shared.WBP, MoveSource, *Shared.OwnedNames, OutReason);
	}

	namespace
	{
		/** Result of MatchNestedChildren: a deterministic pairing of design children with existing owned child widgets. */
		struct FNestedChildrenMatch
		{
			struct FMatchedPair
			{
				const FUMGWidgetNode* Child = nullptr;
				UWidget* Widget = nullptr;
			};
			/** Paired (direct manifest-identity match, or the k-th same-named design child to the k-th suffixed existing widget). */
			TArray<FMatchedPair> Matched;
			/** Design has it, no local owned candidate (a nested add or the target end of a nested move). */
			TArray<const FUMGWidgetNode*> UnmatchedDesignChildren;
			/** Locally owned, no longer expected by the design (a nested-delete leftover or the source end of a nested move). */
			TArray<UWidget*> UnmatchedOwnedWidgets;
		};

		/**
		 * Synthetic wrappers have no design identity or share their only child's identity.
		 * Match through these layout/visual carriers rather than treating them as design nodes.
		 */
		bool IsSyntheticWrapperNode(const FUMGWidgetNode& Node)
		{
			if (!HasRealSourceIdentity(Node))
			{
				return true;
			}
			return Node.Children.Num() == 1
				&& GetDesignNodeId(Node) == GetDesignNodeId(Node.Children[0]);
		}

		/** Outcome of TryMatchChildByManifestRecord (see the function comment for the three states). */
		enum class EManifestMatchOutcome : uint8
		{
			/** No usable record here: the caller falls through to same-base-name matching. */
			Unhandled,
			/** The recorded widget hangs under another parent: the reparent stage owns this child; no base-name attempt. */
			HandledElsewhere,
			/** Paired with the design child. */
			Matched,
		};

		/**
		 * Match manifest instances by recorded identity before matching names.
		 * Matched pairs belong here; HandledElsewhere pairs belong to the reparent pass;
		 * Unhandled children continue with same-base-name matching.
		 */
		EManifestMatchOutcome TryMatchChildByManifestRecord(
			const FUMGWidgetNode& Child,
			UPanelWidget* Panel,
			const FReimportApplySharedState& Shared,
			TArray<UWidget*>& OwnedChildren,
			FNestedChildrenMatch& Out)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* const* RecordPtr =
				Shared.InstancesById->Find(GetDesignNodeId(Child));
			if (!RecordPtr)
			{
				return EManifestMatchOutcome::Unhandled;
			}
			UWidget* Existing = Shared.WidgetTree->FindWidget(FName(*(*RecordPtr)->WidgetName));
			if (!Existing)
			{
				return EManifestMatchOutcome::Unhandled;
			}
			if (Existing->GetParent() != Panel)
			{
				return EManifestMatchOutcome::HandledElsewhere;
			}
			if (OwnedChildren.Contains(Existing))
			{
				Out.Matched.Add({ &Child, Existing });
				OwnedChildren.RemoveSingle(Existing);
				return EManifestMatchOutcome::Matched;
			}
			return EManifestMatchOutcome::Unhandled;
		}

		/**
		 * Pair same-base-name children by generated suffix rank, matching import order.
		 * Unmatched candidates are handled by the caller.
		 */
		bool TryMatchChildByBaseName(
			const FUMGWidgetNode& Child,
			TArray<UWidget*>& OwnedChildren,
			FNestedChildrenMatch& Out)
		{
			const FString BaseName = FUMGWidgetUtils::SanitizeWidgetName(Child.Name, Child.Type).ToString();

			TArray<UWidget*> Candidates;
			for (UWidget* OwnedChild : OwnedChildren)
			{
				if (FUMGWidgetUtils::MatchesWidgetBaseName(OwnedChild->GetFName().ToString(), BaseName))
				{
					Candidates.Add(OwnedChild);
				}
			}
			Candidates.Sort([&BaseName](const UWidget& A, const UWidget& B)
			{
				return FUMGWidgetUtils::GetWidgetNameSuffixRank(A.GetFName().ToString(), BaseName)
					< FUMGWidgetUtils::GetWidgetNameSuffixRank(B.GetFName().ToString(), BaseName);
			});

			if (!Candidates.IsEmpty())
			{
				Out.Matched.Add({ &Child, Candidates[0] });
				OwnedChildren.RemoveSingle(Candidates[0]);
				return true;
			}
			return false;
		}

		/**
		 * Deterministic same-name pairing of the container subtree (count only, no mutation). The executing walk
		 * (RefreshChildWidgets) and the pre-scan (CollectExpectedNestedNames) share this one computation so both passes decide identically.
		 */
		FNestedChildrenMatch MatchNestedChildren(const FUMGWidgetNode& Node, UPanelWidget* Panel, const FReimportApplySharedState& Shared)
		{
			FNestedChildrenMatch Out;

			// Only child widgets inside the owned list participate in the pairing; project-side additions do not.
			TArray<UWidget*> OwnedChildren;
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Child = Panel->GetChildAt(Index))
				{
					if (Shared.OwnedNames->Contains(Child->GetFName().ToString()))
					{
						OwnedChildren.Add(Child);
					}
				}
			}

			// Pair direct design children first, then flatten still-unpaired synthetic wrapper layers into their content.
			// Finite wrapper chains guarantee termination. Paired widgets leave the candidate pool immediately and cannot
			// be paired again at a deeper wrapper level.
			TArray<const FUMGWidgetNode*> PendingChildren;
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				PendingChildren.Add(&Child);
			}

			while (true)
			{
				TArray<const FUMGWidgetNode*> UnmatchedCandidates;
				for (const FUMGWidgetNode* Child : PendingChildren)
				{
					if (TryMatchChildByManifestRecord(*Child, Panel, Shared, OwnedChildren, Out) == EManifestMatchOutcome::Unhandled
						&& !TryMatchChildByBaseName(*Child, OwnedChildren, Out))
					{
						UnmatchedCandidates.Add(Child);
					}
				}

				// Replace still-unpaired synthetic wrapper layers with their content children for the next pairing pass.
				// Content-less synthetic layers (justify spacers etc.) stay unmatched and take the regular rebuild path.
				TArray<const FUMGWidgetNode*> NextPending;
				bool bFlattened = false;
				for (const FUMGWidgetNode* Child : UnmatchedCandidates)
				{
					if (IsSyntheticWrapperNode(*Child) && Child->Children.Num() > 0)
					{
						for (const FUMGWidgetNode& GrandChild : Child->Children)
						{
							NextPending.Add(&GrandChild);
						}
						bFlattened = true;
					}
					else
					{
						NextPending.Add(Child);
					}
				}
				if (!bFlattened)
				{
					Out.UnmatchedDesignChildren = MoveTemp(NextPending);
					break;
				}
				PendingChildren = MoveTemp(NextPending);
			}
			Out.UnmatchedOwnedWidgets = MoveTemp(OwnedChildren);
			return Out;
		}
	}

	/** Collect owned non-instance widgets no longer expected in their original container.
 * Nested refresh can reuse them as move sources; remaining candidates identify removals. */
	void CollectExpectedNestedNames(
		const FUMGWidgetNode& Node,
		UPanelWidget* Panel,
		const FReimportApplySharedState& Shared,
		TSet<FString>& OutExpected)
	{
		const FNestedChildrenMatch Match = MatchNestedChildren(Node, Panel, Shared);
		for (const FNestedChildrenMatch::FMatchedPair& Pair : Match.Matched)
		{
			OutExpected.Add(Pair.Widget->GetFName().ToString());
			// mapped candidates do not expand their subtree (RefreshMappedInstance does not recurse into children).
			if (!IsMappedCandidateNode(*Pair.Child))
			{
				if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(Pair.Widget))
				{
					CollectExpectedNestedNames(*Pair.Child, ChildPanel, Shared, OutExpected);
				}
			}
		}
	}

	namespace
	{
		/**
		 * Move an available D2U-owned subtree while preserving widget identity.
		 * Mapped instances are rebuilt instead. Unset means no candidate; true means moved
		 * or conflict-kept; false means structural failure and must not create a duplicate.
		 */
		TOptional<bool> TryNestedMoveChild(
			const FUMGWidgetNode& Child,
			const FString& BaseName,
			UPanelWidget* Panel,
			UWidget* ParentWidget,
			const FReimportApplySharedState& Shared,
			TArray<UWidget*>& NestedMoveCandidates,
			TMap<FString, UWidget*>& NewlyCreatedWidgets)
		{
			UWidget* MoveSource = nullptr;
			if (!IsMappedCandidateNode(Child))
			{
				for (int32 Index = 0; Index < NestedMoveCandidates.Num(); ++Index)
				{
					UWidget* Candidate = NestedMoveCandidates[Index];
					if (FUMGWidgetUtils::MatchesWidgetBaseName(Candidate->GetFName().ToString(), BaseName))
					{
						MoveSource = Candidate;
						NestedMoveCandidates.RemoveAt(Index);
						break;
					}
				}
			}
			if (!MoveSource)
			{
				return {};
			}

			FString ConflictReason;
			if (!IsMoveSourceOwned(Shared, MoveSource, ConflictReason))
			{
				AddReimportDegradation(
					Shared, ReimportDiagnosticKinds::MoveConflict,
					FString::Printf(
						TEXT("Nested move of '%s' into '%s' skipped: %s; widget kept in place, no copy created"),
						*MoveSource->GetName(), *ParentWidget->GetName(), *ConflictReason));
				return true;
			}

			UPanelWidget* OldParent = MoveSource->GetParent();
			if (!MoveReimportWidget(MoveSource, Panel, Child, Shared)
				|| !RefreshNodeByKind(Child, MoveSource, Shared, NestedMoveCandidates, NewlyCreatedWidgets))
			{
				return false;
			}
			AddReimportDiagnostic(
				*Shared.Diagnostics, ReimportDiagnosticKinds::Moved,
				FString::Printf(
					TEXT("Widget '%s' moved from '%s' under '%s' (nested, both endpoints D2U-owned); widget identity preserved"),
					*MoveSource->GetName(), *OldParent->GetName(), *ParentWidget->GetName()));
			return true;
		}

		/**
		 * Design-added nested nodes go through the generation-chain rebuild (the unmatched-child path of
		 * RefreshChildWidgets): mapped candidates instantiate as usual and their names enter the new d2uOwned;
		 * manifest instances already inside the subtree are pruned (rehung by reparent).
		 */
		bool CreateNestedChildWidget(
			const FUMGWidgetNode& Child,
			UPanelWidget* Panel,
			UWidget* ParentWidget,
			const FReimportApplySharedState& Shared,
			TMap<FString, UWidget*>& NewlyCreatedWidgets)
		{
			FUMGWidgetNode PrunedChild = Child;
			PruneManifestInstanceChildren(PrunedChild, *Shared.InstancesById);
			UWidget* CreatedWidget = CreateReimportWidget(PrunedChild, Panel, Shared);
			if (!CreatedWidget) return false;
			NewlyCreatedWidgets.Add(GetDesignNodeId(Child), CreatedWidget);
			AddReimportDiagnostic(
				*Shared.Diagnostics, ReimportDiagnosticKinds::Added,
				FString::Printf(TEXT("New design node '%s' (%s) added under '%s'"),
					*Child.Name, *GetDesignNodeId(Child), *ParentWidget->GetName()));
			return true;
		}

		/**
		 * Refresh owned subtrees by deterministic widget names; preserve project additions.
		 * Retain removed nested children. For additions, try an owned move before rebuilding;
		 * project-owned source content produces a conflict rather than a duplicate.
		 */
		bool RefreshChildWidgets(
			const FUMGWidgetNode& Node,
			UWidget* Widget,
			const FReimportApplySharedState& Shared,
			TArray<UWidget*>& NestedMoveCandidates,
			TMap<FString, UWidget*>& NewlyCreatedWidgets)
		{
			UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
			if (!Panel)
			{
				return true;
			}

			const FNestedChildrenMatch Match = MatchNestedChildren(Node, Panel, Shared);
			for (const FNestedChildrenMatch::FMatchedPair& Pair : Match.Matched)
			{
				if (!RefreshNodeByKind(*Pair.Child, Pair.Widget, Shared, NestedMoveCandidates, NewlyCreatedWidgets)) return false;
			}

			for (const FUMGWidgetNode* Child : Match.UnmatchedDesignChildren)
			{
				const FString BaseName = FUMGWidgetUtils::SanitizeWidgetName(Child->Name, Child->Type).ToString();
				const TOptional<bool> Moved = TryNestedMoveChild(
					*Child, BaseName, Panel, Widget, Shared, NestedMoveCandidates, NewlyCreatedWidgets);
				if (Moved.IsSet())
				{
					if (!Moved.GetValue()) return false;
				}
				else if (!CreateNestedChildWidget(*Child, Panel, Widget, Shared, NewlyCreatedWidgets))
				{
					return false;
				}
			}
			return true;
		}
	}

	bool RefreshNodeByKind(
		const FUMGWidgetNode& Node,
		UWidget* Widget,
		const FReimportApplySharedState& Shared,
		TArray<UWidget*>& NestedMoveCandidates,
		TMap<FString, UWidget*>& NewlyCreatedWidgets)
	{
		if (IsMappedCandidateNode(Node))
		{
			if (UUserWidget* MappedInstance = Cast<UUserWidget>(Widget))
			{
				RefreshMappedInstance(MappedInstance, Node, Shared);
				return true;
			}
			if (Shared.GenerationSettings->ComponentBlueprints)
			{
				// A failed explicit mapping was expanded normally, not generated as a child WBP.
				// Its descendants were also excluded from automatic dependency planning.
				FDesign2UMGGenerationSettingsSnapshot Settings = *Shared.GenerationSettings;
				Settings.ComponentBlueprints.Reset();
				FReimportApplySharedState Ordinary = Shared;
				Ordinary.GenerationSettings = &Settings;
				return RefreshNodeByKind(Node, Widget, Ordinary, NestedMoveCandidates, NewlyCreatedWidgets);
			}
		}

		const auto ComponentContext = Shared.GenerationSettings->ComponentBlueprints;
		const FString AutomaticKey = ComponentContext ? ComponentContext->ResolveKey(Node) : FString();
		if (!AutomaticKey.IsEmpty())
		{
			Widget = RefreshAutomaticInstance(Node, Widget, Shared);
			if (!Widget) return false;
		}
		Widget->Modify();
		ApplySlotForNode(Widget, Node, Shared);
		if (AutomaticKey.IsEmpty()) FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);
		if (AutomaticKey.IsEmpty()
			&& !RefreshChildWidgets(Node, Widget, Shared, NestedMoveCandidates, NewlyCreatedWidgets)) return false;

		// Only top-level generation relations (hung under the host root) enter the manifest instances; nested nodes only refresh properties.
		if (Shared.HostPanel && Widget->GetParent() == Shared.HostPanel)
		{
			FDesign2UMGGeneratedInstanceRecord Record;
			Record.DesignNodeId = GetDesignNodeId(Node);
			Record.WidgetName = Widget->GetFName().ToString();
			Shared.UpdatedGeneratedRecords->Add(MoveTemp(Record));
		}
		// Property-refresh diagnostics are as granular as the top level: nested D2U-owned node refreshes are also named per node.
		AddReimportDiagnostic(
			*Shared.Diagnostics, ReimportDiagnosticKinds::Updated,
			FString::Printf(
				TEXT("D2U-owned node '%s' (%s) refreshed from design (layout/slot/visual/text)"),
				*Node.Name, *GetDesignNodeId(Node)));
		return true;
	}}
