// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "Design2UMGPrototypeGraphWiring.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "WidgetBlueprint.h"

namespace DesignImportPipeline
{
	// ---------------------------------------------------------------------------
	// Reimport main flow
	// ---------------------------------------------------------------------------

	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request)
	{
		FDesignReimportResult Result;

		// 1)-3) Settings snapshot / re-parse the source / locate the target asset + read the manifest.
		FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
		FUMGWidgetData Data;
		FString TargetPackage;
		UWidgetBlueprint* WBP = nullptr;
		FDesign2UMGGenerationManifestSidecar Manifest;
		if (TOptional<FDesignReimportResult> Early = ResolveReimportSourceAndManifest(
			Document, Request, Result, SettingsSnapshot, Data, TargetPackage, WBP, Manifest))
		{
			return MoveTemp(*Early);
		}
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings = SettingsSnapshot.GenerationSettings;
		const FReimportSession Session{ Document, Request, Data, GenerationSettings };

		// 4)-5) Scope matching, source-root location and host continuation.
		const FUMGWidgetNode* ScopeRoot = nullptr;
		bool bPrototypeHostContinued = false;
		if (TOptional<FDesignReimportResult> Early = MatchReimportScopeAndRoot(
			Session, Manifest, Result, ScopeRoot, bPrototypeHostContinued))
		{
			return MoveTemp(*Early);
		}

		// 6)-8) Component Mapping two-tier match, update-plan construction, structural sanity check.
		FUMGWidgetNode PreparedScopeRoot;
		FDesign2UMGComponentMappingBuildState MappingBuildState(GenerationSettings.ComponentMapping);
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*> InstancesById;
		TArray<FReimportPlanEntry> Plan;
		TSet<FString> AliveInstanceWidgetNames;
		BuildReimportUpdatePlan(
			GenerationSettings, Data, Manifest, ScopeRoot,
			PreparedScopeRoot, MappingBuildState, InstancesById, Plan, AliveInstanceWidgetNames);
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		if (TOptional<FDesignReimportResult> Early = CheckReimportStructureViability(
			Session, WBP, Manifest, PreparedScopeRoot, Result, DesignSize))
		{
			return MoveTemp(*Early);
		}

		// 9) Apply the update plan inside an editor transaction.
		const int32 TransactionIndex = BeginReimportApplyTransaction(WBP);

		// Generated prototype navigation wiring is plugin-owned transient content, re-authored by the
		// save (step 11) from the new source. Clear it before the tree updates so the safe-delete /
		// identity-conversion gates ("referenced by blueprint graphs") only ever see user-authored nodes.
		Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(WBP);
		TArray<FDesign2UMGMappedInstanceRecord> FinalMappedRecords;
		TArray<FDesign2UMGGeneratedInstanceRecord> FinalGeneratedRecords;
		TSet<FString> UpdatedD2UOwned(Manifest.D2UOwned);
		const TSet<FString> ManifestOwnedNames(Manifest.D2UOwned);
		TArray<FString> KeepAsIsWidgetNames;
		// Widgets newly created by this reimport round (designNodeId -> widget), used by reparent target resolution:
		// new containers (design-added nodes) are also clearly D2U-owned.
		TMap<FString, UWidget*> NewlyCreatedWidgets;

		FReimportApplySharedState Shared;
		Shared.WBP = WBP;
		Shared.WidgetTree = WBP->WidgetTree;
		Shared.HostPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
		Shared.DesignSize = DesignSize;
		Shared.OwnedNames = &ManifestOwnedNames;
		Shared.InstancesById = &InstancesById;
		Shared.MappingBuildState = &MappingBuildState;
		Shared.Diagnostics = &Result.Diagnostics;
		Shared.UpdatedMappedRecords = &FinalMappedRecords;
		Shared.UpdatedGeneratedRecords = &FinalGeneratedRecords;
		Shared.AliveInstanceWidgetNames = &AliveInstanceWidgetNames;
		MappingBuildState.SetRootWidget(WBP->WidgetTree->RootWidget);

		// 9a)-9d) removed / kind-changed / added / reparent。The mutable cross-stage channels
		// (UpdatedD2UOwned / KeepAsIsWidgetNames / NewlyCreatedWidgets) flow through the stage
		// signatures: 9a/9b delete names out of the owned list and record conflict keeps; 9b/9c
		// register newly created widgets; 9d reparent resolves its targets from that registry.
		ApplyRemovedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames);
		ApplyKindChangedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames, NewlyCreatedWidgets);
		ApplyAddedTopLevelNodes(PreparedScopeRoot, Shared, NewlyCreatedWidgets);
		ApplyReparentPlanEntries(Plan, PreparedScopeRoot, Shared, KeepAsIsWidgetNames, NewlyCreatedWidgets);

		// 9d-2) Nested-structure diff pre-scan (the move-candidate pool). Must run after 9c created the
		// new widgets and 9a/9b deleted the stale ones - expressed here by the stage ordering itself:
		// the pool returned here is the input of the 9e refresh and the 9g leftover diagnostics below.
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Shared);

		// 9e)-9g) Property updates / nested-delete leftover diagnostics (step comments live with their functions).
		ApplyMatchedPlanEntries(Plan, Shared, GenerationSettings, NestedMoveCandidates, NewlyCreatedWidgets);
		ReportNestedRemovalLeftovers(Shared, NestedMoveCandidates);

		// 9h) Re-author the generated transition animations for the updated tree (name-prefix purge inside
		// keeps regenerates from accumulating); the enriched navigation copy carries the animation names
		// into the save, whose wiring plays them around the page switch.
		FUMGPrototypeNavigationData ReimportNavigation = Session.Data.PrototypeNavigation();
		UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(WBP, PreparedScopeRoot, ReimportNavigation);

		// Mapping diagnostics exit (aligned with the import path's UMGGenerator assembly): the ini validation
		// diagnostics + this round's BuildState-collected match/fallback diagnostics go into
		// ComponentMappingDiagnostics, written by the report into the componentMapping section; previously the reimport report lacked this exit, hiding config errors like class-load failures.
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// 10) Assemble the post-reimport manifest payload + asset finalization (CDO preview size, variable GUIDs).
		FDesign2UMGGenerationOutput GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames);
		FinalizeReimportedAssetState(WBP, DesignSize);

		// 11) Writability pre-check + save + FailSaveWithRollback.
		return SaveReimportedAsset(
			Session, TargetPackage, WBP, GenerationOutput, ReimportNavigation, TransactionIndex, Result);
	}
}
