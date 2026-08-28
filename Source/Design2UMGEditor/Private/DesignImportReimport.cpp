// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

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
		TArray<FDesign2UMGMappedInstanceRecord> FinalMappedRecords;
		TArray<FDesign2UMGGeneratedInstanceRecord> FinalGeneratedRecords;
		TSet<FString> UpdatedD2UOwned(Manifest.D2UOwned);
		const TSet<FString> ManifestOwnedNames(Manifest.D2UOwned);
		TArray<FString> KeepAsIsWidgetNames;
		// Widgets newly created by this reimport round (designNodeId -> widget), used by reparent target resolution:
		// new containers (design-added nodes) are also clearly D2U-owned.
		TMap<FString, UWidget*> NewlyCreatedWidgets;

		FReimportApplyContext Ctx;
		Ctx.WBP = WBP;
		Ctx.WidgetTree = WBP->WidgetTree;
		Ctx.HostPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
		Ctx.DesignSize = DesignSize;
		Ctx.OwnedNames = &ManifestOwnedNames;
		Ctx.InstancesById = &InstancesById;
		Ctx.MappingBuildState = &MappingBuildState;
		Ctx.Diagnostics = &Result.Diagnostics;
		Ctx.UpdatedMappedRecords = &FinalMappedRecords;
		Ctx.UpdatedGeneratedRecords = &FinalGeneratedRecords;
		Ctx.NewlyCreatedWidgets = &NewlyCreatedWidgets;
		Ctx.UpdatedD2UOwned = &UpdatedD2UOwned;
		Ctx.KeepAsIsWidgetNames = &KeepAsIsWidgetNames;
		Ctx.AliveInstanceWidgetNames = &AliveInstanceWidgetNames;
		MappingBuildState.SetRootWidget(WBP->WidgetTree->RootWidget);

		// 9a)-9d) removed / kind-changed / added / reparent。
		ApplyRemovedPlanEntries(Plan, Ctx);
		ApplyKindChangedPlanEntries(Plan, Ctx);
		ApplyAddedTopLevelNodes(PreparedScopeRoot, Ctx);
		ApplyReparentPlanEntries(Plan, PreparedScopeRoot, Ctx);

		// 9d-2) Nested-structure diff pre-scan (the move-candidate pool).
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Ctx);
		Ctx.NestedMoveCandidates = &NestedMoveCandidates;

		// 9e)-9g) Property updates / nested-delete leftover diagnostics (step comments live with their functions).
		ApplyMatchedPlanEntries(Plan, Ctx, GenerationSettings);
		// 9f) Prototype binding refresh (ordinary generated widgets, by stable design identity; double-trigger yields).
		ApplyPrototypeRefresh(Data, GenerationSettings, Ctx);
		ReportNestedRemovalLeftovers(Ctx);

		// Mapping diagnostics exit (aligned with the import path's UMGGenerator assembly): the ini validation
		// diagnostics + this round's BuildState-collected match/fallback diagnostics go into
		// ComponentMappingDiagnostics, written by the report into the componentMapping section; previously the reimport report lacked this exit, hiding config errors like class-load failures.
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// 10) Assemble the post-reimport manifest payload + asset finalization (CDO preview size, variable GUIDs).
		FDesign2UMGGenerationOutput GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Ctx);
		FinalizeReimportedAssetState(Ctx);

		// 11) Writability pre-check + save + FailSaveWithRollback.
		return SaveReimportedAsset(Session, TargetPackage, WBP, GenerationOutput, TransactionIndex, Result);
	}
}
