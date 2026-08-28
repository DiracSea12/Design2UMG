// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "Design2UMGComponentMappingGeneration.h"
#include "DesignImportPipeline.h"
#include "UMGGenerator.h"

struct FDesignDocument;
struct FUMGPrototypeNavigationBinding;

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
	 * HandleFullConflict (mirroring the apply-stage FReimportApplyContext so the Document/Request/
	 * Data/GenerationSettings quartet is not repeated verbatim in every early-exit signature and every
	 * HandleFullConflict call site). GenerationSettings is available after settings-snapshot resolution (step 1).
	 */
	struct FReimportSession
	{
		FDesignDocument& Document;
		const FDesignReimportRequest& Request;
		FUMGWidgetData& Data;
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings;
	};

	struct FReimportApplyContext
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
		/** Widgets newly created this round (designNodeId -> widget); referenced when resolving reparent targets. */
		TMap<FString, UWidget*>* NewlyCreatedWidgets = nullptr;
		/**
		 * Move-candidate pool of the nested-structure diff (collected by the main-flow pre-scan): widgets inside the owned
		 * list, not manifest instances, and no longer expected by the new design to stay in their original container.
		 * The container-subtree diff consumes move sources from the pool by deterministic same-name; unconsumed ones go to leftover-deletion diagnostics (pure-D2U silently kept).
		 */
		TArray<UWidget*>* NestedMoveCandidates = nullptr;
		/** The 9a-10 shared owned-list evolution (initial = manifest.d2uOwned, updated as deletions/additions happen). */
		TSet<FString>* UpdatedD2UOwned = nullptr;
		/** Conflict-kept list (9a/9b keeps; 10 backfills manifest records). */
		TArray<FString>* KeepAsIsWidgetNames = nullptr;
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
	bool HasProjectBindingForInteraction(
		const UWidgetBlueprint* WBP,
		const FUMGPrototypeNavigationBinding& Binding);
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
	void ApplySlotForNode(UWidget* Widget, const FUMGWidgetNode& Node, const FReimportApplyContext& Ctx);
	bool IsMappedClassConflict(
		const UWidget* Instance,
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record,
		const FString& CurrentMappedClass);
	bool IsMoveSourceOwned(const FReimportApplyContext& Ctx, UWidget* MoveSource, FString& OutReason);
	void CollectExpectedNestedNames(
		const FUMGWidgetNode& Node,
		UPanelWidget* Panel,
		const FReimportApplyContext& Ctx,
		TSet<FString>& OutExpected);
	void RefreshNodeByKind(const FUMGWidgetNode& Node, UWidget* Widget, FReimportApplyContext& Ctx);
	void ApplyPrototypeRefresh(
		FUMGWidgetData& Data,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
		FReimportApplyContext& Ctx);

	// DesignImportReimportStages.cpp
	FDesign2UMGMappedInstanceRecord ManifestRecordToMappedRecord(
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record);
	void ApplyRemovedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx);
	void ApplyKindChangedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx);
	void ApplyAddedTopLevelNodes(
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx);
	void ApplyReparentPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx);
	TArray<UWidget*> CollectNestedMoveCandidates(
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportApplyContext& Ctx);
	void ApplyMatchedPlanEntries(
		const TArray<FReimportPlanEntry>& Plan,
		FReimportApplyContext& Ctx,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
	void ReportNestedRemovalLeftovers(FReimportApplyContext& Ctx);

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
		FReimportApplyContext& Ctx);
	void FinalizeReimportedAssetState(FReimportApplyContext& Ctx);
	FDesignReimportResult SaveReimportedAsset(
		const FReimportSession& Session,
		const FString& TargetPackage,
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput& GenerationOutput,
		int32 TransactionIndex,
		FDesignReimportResult& Result);
}
