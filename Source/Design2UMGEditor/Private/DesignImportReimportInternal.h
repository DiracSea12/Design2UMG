// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "Design2UMGComponentMappingGeneration.h"
#include "DesignImportPipeline.h"
#include "UMGGenerator.h"

struct FDesignDocument;

class UPanelWidget;
class UWidget;
class UWidgetBlueprint;
class UWidgetTree;

namespace DesignImportPipeline
{
	// ---------------------------------------------------------------------------
	// Update plan
	// ---------------------------------------------------------------------------

	// Recorded identities: Matched = PropertyUpdate, plus Move when the parent changes;
	// KindChanged = Replace; Removed = Remove. Add is discovered from unmatched new nodes
	// in ApplyAddedTopLevelNodes / RefreshChildWidgets, so it has no old manifest record.
	enum class EReimportPlanAction : uint8
	{
		Matched,      // designNodeId present on both sides with the same kind -> update D2U-owned properties/layout (incl. the reparent decision)
		KindChanged,  // present on both sides but the mapped<->generated kind flipped -> handled by the identity-conversion rules
		Removed,      // in the manifest but not in the new IR -> safe-delete decision
	};

	struct FReimportPlanEntry
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ManifestRecord = nullptr;
		const FUMGWidgetNode* NewNode = nullptr;
		EReimportPlanAction Action = EReimportPlanAction::Matched;
		bool bNewKindMapped = false;
	};

	/** Non-owning lookups over immutable prepared IR. Widget-name lookups are rebuilt after structural
 * updates and used only while reparenting leaves widget identities unchanged. */
	struct FReimportTreeIndex
	{
		TMap<FString, const FUMGWidgetNode*> DesignNodesById;
		TSet<FString> AmbiguousDesignNodeIds;
		TMap<const FUMGWidgetNode*, const FUMGWidgetNode*> ParentsByNode;
		TMap<FName, UWidget*> WidgetsByName;
	};

	/**
	 * Reimport session state shared across stages: the common parameter carrier of the early-exit functions and
	 * HandleFullConflict (so the Document/Request/Data/GenerationSettings quartet is not repeated verbatim in every
	 * early-exit signature and every HandleFullConflict call site). GenerationSettings is available after
	 * settings-snapshot resolution.
	 */
	struct FReimportSession
	{
		/** Null when the outer component batch owns diagnostic reporting. */
		FDesignDocument* Document;
		const FDesignReimportRequest& Request;
		FUMGWidgetData& Data;
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings;
	};

	/**
	 * Reimport diagnostic kind values (the reimport report JSON contract). Tests assert these strings as protocol
	 * anchors; changing any value breaks report consumers - keep them byte-identical.
	 */
	namespace ReimportDiagnosticKinds
	{
		inline constexpr const TCHAR* PrepareFailed = TEXT("reimport_prepare_failed");
		inline constexpr const TCHAR* AttachFailed = TEXT("reimport_attach_failed");
		inline constexpr const TCHAR* ApplyFailed = TEXT("reimport_apply_failed");
		inline constexpr const TCHAR* Added = TEXT("reimport_added");
		inline constexpr const TCHAR* Completed = TEXT("reimport_completed");
		inline constexpr const TCHAR* FullConflict = TEXT("reimport_full_conflict");
		inline constexpr const TCHAR* InputSkipped = TEXT("reimport_input_skipped");
		inline constexpr const TCHAR* KindChanged = TEXT("reimport_kind_changed");
		inline constexpr const TCHAR* KindConflict = TEXT("reimport_kind_conflict");
		inline constexpr const TCHAR* MappedAttrKept = TEXT("reimport_mapped_attr_kept");
		inline constexpr const TCHAR* MappedClassConflict = TEXT("reimport_mapped_class_conflict");
		inline constexpr const TCHAR* MoveConflict = TEXT("reimport_move_conflict");
		inline constexpr const TCHAR* Moved = TEXT("reimport_moved");
		inline constexpr const TCHAR* NodeMissing = TEXT("reimport_node_missing");
		inline constexpr const TCHAR* PrototypeHostContinued = TEXT("reimport_prototype_host_continued");
		inline constexpr const TCHAR* PrototypeGraphConflict = TEXT("reimport_prototype_graph_conflict");
		inline constexpr const TCHAR* PartialSync = TEXT("reimport_partial_sync");
		inline constexpr const TCHAR* RemoveConflict = TEXT("reimport_remove_conflict");
		inline constexpr const TCHAR* Removed = TEXT("reimport_removed");
		inline constexpr const TCHAR* ResolutionCopy = TEXT("reimport_resolution_copy");
		inline constexpr const TCHAR* SaveFailed = TEXT("reimport_save_failed");
		inline constexpr const TCHAR* ScopeMatched = TEXT("reimport_scope_matched");
		inline constexpr const TCHAR* Updated = TEXT("reimport_updated");
	}

	/**
	 * Apply-stage bindings: asset/tree snapshot, manifest indexes and append-only output sinks.
	 * Mutable move, ownership and newly-created-widget sets remain explicit stage parameters.
	 */
	struct FReimportApplySharedState
	{
		UWidgetBlueprint* WBP = nullptr;
		UWidgetTree* WidgetTree = nullptr;
		UPanelWidget* HostPanel = nullptr;
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		/** Captured generation settings (class overrides et al.): every rebuild path must thread them, matching the import-time generation chain. */
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr;
		/** Set view of the manifest.d2uOwned list (data source of the delete/move ownership decisions). */
		const TSet<FString>* OwnedNames = nullptr;
		const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>* InstancesById = nullptr;
		const FReimportTreeIndex* TreeIndex = nullptr;
		FDesign2UMGComponentMappingBuildState* MappingBuildState = nullptr;
		TArray<FDesign2UMGMappingDiagnostic>* Diagnostics = nullptr;
		/** Monotonic non-fatal partial-sync signal; diagnostics explain the concrete cause. */
		bool* bHasDegradation = nullptr;
		TArray<FDesign2UMGMappedInstanceRecord>* UpdatedMappedRecords = nullptr;
		TArray<FDesign2UMGGeneratedInstanceRecord>* UpdatedGeneratedRecords = nullptr;
		/** Manifest instance names still present in the update plan, protected from subtree deletion. */
		const TSet<FString>* AliveInstanceWidgetNames = nullptr;
	};

	// ---------------------------------------------------------------------------
	// Cross-file declarations (defined in the DesignImportReimport*.cpp files)
	// ---------------------------------------------------------------------------

	// DesignImportReimportDiagnostics.cpp
	void AddReimportDiagnostic(
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics,
		const TCHAR* Kind,
		const FString& Message);
	void AddReimportDegradation(
		const FReimportApplySharedState& Shared,
		const TCHAR* Kind,
		const FString& Message);
	bool HasRealSourceIdentity(const FUMGWidgetNode& Node);
	bool OwnsSourceIdentity(const FUMGWidgetNode& Node);
	FString GetDesignNodeId(const FUMGWidgetNode& Node);
	void PruneManifestInstanceChildren(
		FUMGWidgetNode& Node,
		const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById);

	// DesignImportReimportConflict.cpp
	bool IsSubtreePurelyD2UOwned(
		const UWidgetBlueprint* WBP,
		UWidget* SubtreeRoot,
		const TSet<FString>& D2UOwned,
		FString& OutReason);
	TSet<FString> DeleteD2UOwnedSubtreeSilently(
		UWidgetBlueprint* WBP, UWidget* SubtreeRoot, TSet<FString>& InOutUpdatedD2UOwned);
	bool CollectLiveInstancesInSubtree(
		UWidget* SubtreeRoot,
		const TSet<FString>& AliveInstanceWidgetNames,
		const FString& ExcludeWidgetName,
		TArray<FString>& OutLiveNames);
	FDesignReimportResult HandleFullConflict(
		const FString& Reason,
		const FReimportSession& Session,
		TArray<FDesign2UMGMappingDiagnostic>&& PriorDiagnostics);

	bool DiscardDetachedReimportWidgets(UWidgetTree* WidgetTree, const TSet<UObject*>& ExistingObjects,
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics);
	// DesignImportReimportBuild.cpp: construction is detached; ownership is committed after attachment.
	UWidget* CreateReimportWidget(const FUMGWidgetNode& Node, UPanelWidget* Parent,
		const FReimportApplySharedState& Shared);
	UWidget* ReplaceReimportWidget(const FUMGWidgetNode& Node, UWidget* OldWidget,
		const FReimportApplySharedState& Shared, TSet<FString>& UpdatedD2UOwned);
	bool MoveReimportWidget(UWidget* Widget, UPanelWidget* Target, const FUMGWidgetNode& Node,
		const FReimportApplySharedState& Shared);
	UWidget* RefreshAutomaticInstance(const FUMGWidgetNode& Node, UWidget* Widget,
		const FReimportApplySharedState& Shared);

	// DesignImportReimportApply.cpp
	void ApplySlotForNode(UWidget* Widget, const FUMGWidgetNode& Node, const FReimportApplySharedState& Shared);
	bool IsMappedClassConflict(
		const UWidget* Instance,
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record,
		const FString& CurrentMappedClass);
	bool IsMoveSourceOwned(const FReimportApplySharedState& Shared, UWidget* MoveSource, FString& OutReason);
	void CollectExpectedNestedNames(
		const FUMGWidgetNode& Node,
		UPanelWidget* Panel,
		const FReimportApplySharedState& Shared,
		TSet<FString>& OutExpected);
	bool RefreshNodeByKind(
		const FUMGWidgetNode& Node,
		UWidget* Widget,
		const FReimportApplySharedState& Shared,
		TArray<UWidget*>& NestedMoveCandidates,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);

	// DesignImportReimportStages.cpp
	FDesign2UMGMappedInstanceRecord ManifestRecordToMappedRecord(
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record);
	bool ApplyRemovedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames);
	bool ApplyKindChangedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	bool ApplyAddedTopLevelNodes(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	bool ApplyReparentPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	TArray<UWidget*> CollectNestedMoveCandidates(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared);
	bool ApplyMatchedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
		TArray<UWidget*>& NestedMoveCandidates,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	void ReportNestedRemovalLeftovers(
		const FReimportApplySharedState& Shared,
		const TArray<UWidget*>& NestedMoveCandidates);

	// DesignImportReimportPersist.cpp
	TOptional<FDesignReimportResult> ResolveReimportSourceAndManifest(
		FDesignDocument& Document,
		const FDesignReimportRequest& Request,
		FDesignReimportResult& Result,
		FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& Data,
		FString& TargetPackage,
		UWidgetBlueprint*& WBP,
		FDesign2UMGGenerationManifestSidecar& Manifest);
	TOptional<FDesignReimportResult> PrepareReimportScope(
		const FReimportSession& Session,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesignReimportResult& Result,
		FUMGWidgetNode& PreparedRoot,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		const FUMGWidgetNode*& ScopeRoot,
		bool& bPrototypeHostContinued);
	bool BuildReimportUpdatePlan(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportTreeIndex& TreeIndex,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById,
		TArray<FReimportPlanEntry>& Plan,
		TSet<FString>& AliveInstanceWidgetNames,
		FString& OutFailureReason);
	TOptional<FDesignReimportResult> CheckReimportStructureViability(
		const FReimportSession& Session,
		UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportTreeIndex& TreeIndex,
		FDesignReimportResult& Result,
		FIntPoint& DesignSize);
	void RebuildReimportWidgetNameIndex(UWidgetTree* WidgetTree, FReimportTreeIndex& TreeIndex);
	int32 BeginReimportApplyTransaction(UWidgetBlueprint* WBP);
	FDesign2UMGGenerationOutput BuildReimportManifestPayload(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		bool bPrototypeHostContinued,
		const FUMGWidgetData& Data,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		const TArray<FString>& KeepAsIsWidgetNames);
	void EnsureReimportWidgetGuids(UWidgetBlueprint* WBP);
	void FinalizeReimportedAssetState(UWidgetBlueprint* WBP, FIntPoint DesignSize);
	/** Apply the existing ownership merge without saving; caller owns transaction completion. */
	FDesignReimportResult ApplyPreparedReimport(
		const FReimportSession& Session, UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesign2UMGGenerationOutput& GenerationOutput,
		FUMGPrototypeNavigationData& Navigation, int32& TransactionIndex);
	FDesignReimportResult SaveReimportedAsset(
		const FReimportSession& Session,
		const FString& TargetPackage,
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput& GenerationOutput,
		const FUMGPrototypeNavigationData& PrototypeNavigation,
		int32 TransactionIndex,
		FDesignReimportResult& Result);
}
