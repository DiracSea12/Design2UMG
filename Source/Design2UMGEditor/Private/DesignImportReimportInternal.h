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

	/**
	 * Reimport session state shared across stages: the common parameter carrier of the early-exit functions and
	 * HandleFullConflict (so the Document/Request/Data/GenerationSettings quartet is not repeated verbatim in every
	 * early-exit signature and every HandleFullConflict call site). GenerationSettings is available after
	 * settings-snapshot resolution (step 1).
	 */
	struct FReimportSession
	{
		FDesignDocument& Document;
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
		inline constexpr const TCHAR* RemoveConflict = TEXT("reimport_remove_conflict");
		inline constexpr const TCHAR* Removed = TEXT("reimport_removed");
		inline constexpr const TCHAR* ResolutionCopy = TEXT("reimport_resolution_copy");
		inline constexpr const TCHAR* SaveFailed = TEXT("reimport_save_failed");
		inline constexpr const TCHAR* ScopeMatched = TEXT("reimport_scope_matched");
		inline constexpr const TCHAR* Updated = TEXT("reimport_updated");
	}

	/**
	 * Apply-stage shared session state, constructed complete before the 9a) plan stages run:
	 * - read-only bindings: the asset/tree snapshot (WBP/WidgetTree/HostPanel/DesignSize) and the manifest lookup
	 *   tables (OwnedNames/InstancesById/AliveInstanceWidgetNames);
	 * - append-only sinks bound once at construction (diagnostics, the updated manifest record accumulators, the
	 *   mapping BuildState) - pointers never get rebound mid-flow.
	 * Mutable cross-stage channels deliberately do NOT live here: UpdatedD2UOwned / KeepAsIsWidgetNames /
	 * NewlyCreatedWidgets / NestedMoveCandidates travel as explicit stage parameters, so each stage signature
	 * states what it consumes and what it produces (the previous late-bound FReimportApplyContext pointers made the
	 * stage ordering an implicit comment-only contract).
	 */
	struct FReimportApplySharedState
	{
		UWidgetBlueprint* WBP = nullptr;
		UWidgetTree* WidgetTree = nullptr;
		UPanelWidget* HostPanel = nullptr;
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		/** Set view of the manifest.d2uOwned list (data source of the delete/move ownership decisions). */
		const TSet<FString>* OwnedNames = nullptr;
		const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>* InstancesById = nullptr;
		FDesign2UMGComponentMappingBuildState* MappingBuildState = nullptr;
		TArray<FDesign2UMGMappingDiagnostic>* Diagnostics = nullptr;
		TArray<FDesign2UMGMappedInstanceRecord>* UpdatedMappedRecords = nullptr;
		TArray<FDesign2UMGGeneratedInstanceRecord>* UpdatedGeneratedRecords = nullptr;
		/** Manifest instance widget names still alive in this round's update plan (the second gate of the 9a/9b whole delete). */
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
	bool HasRealSourceIdentity(const FUMGWidgetNode& Node);
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
	void RefreshNodeByKind(
		const FUMGWidgetNode& Node,
		UWidget* Widget,
		const FReimportApplySharedState& Shared,
		TArray<UWidget*>& NestedMoveCandidates,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);

	// DesignImportReimportStages.cpp
	FDesign2UMGMappedInstanceRecord ManifestRecordToMappedRecord(
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record);
	void ApplyRemovedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames);
	void ApplyKindChangedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	void ApplyAddedTopLevelNodes(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	void ApplyReparentPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared,
		TArray<FString>& KeepAsIsWidgetNames,
		TMap<FString, UWidget*>& NewlyCreatedWidgets);
	TArray<UWidget*> CollectNestedMoveCandidates(
		const FUMGWidgetNode& PreparedScopeRoot,
		const FReimportApplySharedState& Shared);
	void ApplyMatchedPlanEntries(
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
	TOptional<FDesignReimportResult> MatchReimportScopeAndRoot(
		const FReimportSession& Session,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesignReimportResult& Result,
		const FUMGWidgetNode*& ScopeRoot,
		bool& bPrototypeHostContinued);
	void BuildReimportUpdatePlan(
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
		const FUMGWidgetData& Data,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode* ScopeRoot,
		FUMGWidgetNode& PreparedScopeRoot,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById,
		TArray<FReimportPlanEntry>& Plan,
		TSet<FString>& AliveInstanceWidgetNames);
	TOptional<FDesignReimportResult> CheckReimportStructureViability(
		const FReimportSession& Session,
		UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FDesignReimportResult& Result,
		FIntPoint& DesignSize);
	int32 BeginReimportApplyTransaction(UWidgetBlueprint* WBP);
	FDesign2UMGGenerationOutput BuildReimportManifestPayload(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		bool bPrototypeHostContinued,
		const FUMGWidgetData& Data,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		const TArray<FString>& KeepAsIsWidgetNames);
	void FinalizeReimportedAssetState(UWidgetBlueprint* WBP, FIntPoint DesignSize);
	FDesignReimportResult SaveReimportedAsset(
		const FReimportSession& Session,
		const FString& TargetPackage,
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput& GenerationOutput,
		int32 TransactionIndex,
		FDesignReimportResult& Result);
}
