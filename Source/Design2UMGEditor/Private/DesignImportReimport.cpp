// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"
#include "GeneratedAssetLifecycle.h"

#include "Design2UMGPrototypeGraphWiring.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGComponentBlueprints.h"
#include "DesignImportDiagnosticReport.h"
#include "Misc/PackageName.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectHash.h"

namespace DesignImportPipeline
{
	// ---------------------------------------------------------------------------
	// Reimport main flow
	// ---------------------------------------------------------------------------

	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request)
	{
		FDesignReimportResult Result;
		Result.Commit.AssetPath = Request.TargetAssetPath;

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
		Result.Commit.AssetPath = WBP->GetPathName();
		SettingsSnapshot.GenerationSettings.ComponentConversionSettings = SettingsSnapshot.ConversionSettings;
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings = SettingsSnapshot.GenerationSettings;
		const FReimportSession Session{ &Document, Request, Data, GenerationSettings };
		if (!Manifest.bComponentBatchRoot && !Manifest.GeneratedAssetHash.IsEmpty())
		{
			Result.bFullConflict = true;
			Result.Commit.Status = EDesign2UMGCommitStatus::Conflict;
			AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_component_batch_root_required"),
				TEXT("Select the parent WBP created by the original import to update this component and its dependencies together. This selected asset is a generated child WBP; its saved content was left unchanged."));
			DesignImportDiagnosticReport::WriteReimport(Document, Data, TargetPackage, Result.Diagnostics, nullptr, &Result.Commit);
			return Result;
		}
		if (Manifest.bComponentBatchRoot)
		{
			if (Data.SourceDocument().ComponentDependenciesJson.IsEmpty())
			{
				Result.bFullConflict = true;
				Result.Commit.Status = EDesign2UMGCommitStatus::Conflict;
				AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_component_dependencies_missing"),
					TEXT("This asset owns a component batch; the new source is missing its dependency bundle"));
				return Result;
			}
			FDesign2UMGGenerationOutput Output;
			FUMGPrototypeNavigationData Navigation;
			UWidgetBlueprint* Updated = FDesign2UMGComponentBlueprintContext::Build(Data,
				FPackageName::GetLongPackagePath(TargetPackage), &GenerationSettings, &Output, &Navigation, WBP);
			Result.Commit = Updated ? FUMGGenerator::SaveWidgetBlueprint(Updated, &Output, &Navigation) : Output.Commit;
			Result.bHasDegradation = Output.bHasDegradation || !Data.Warnings.IsEmpty();
			if (Result.Commit.Status == EDesign2UMGCommitStatus::Succeeded && Result.bHasDegradation)
				Result.Commit.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
			Result.bSucceeded = Result.Commit.IsSuccess();
			Result.UpdatedBlueprint = Result.Commit.Asset == EDesign2UMGFileState::Saved ? Updated : nullptr;
			// Batch generation also carries the page ownership merge events. Route those
			// back to the reimport channel rather than hiding them among mapping hits.
			for (FDesign2UMGMappingDiagnostic& Diagnostic : Output.MappingDiagnostics)
			{
				auto& Destination = Diagnostic.Kind.StartsWith(TEXT("reimport_"))
					? Result.Diagnostics : Result.ComponentMappingDiagnostics;
				Destination.Add(MoveTemp(Diagnostic));
			}
			Result.bFullConflict = Result.Commit.Status == EDesign2UMGCommitStatus::Conflict;
			AddReimportDiagnostic(Result.Diagnostics, Result.bSucceeded ? TEXT("reimport_completed") : TEXT("reimport_component_batch_failed"),
				FString::Printf(TEXT("Component batch update %s: %s"), Result.bSucceeded ? TEXT("completed") : TEXT("failed; see component diagnostics"), *TargetPackage));
			DesignImportDiagnosticReport::WriteReimport(Document, Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit);
			return Result;
		}

		FDesign2UMGGenerationOutput GenerationOutput;
		FUMGPrototypeNavigationData ReimportNavigation;
		int32 TransactionIndex = INDEX_NONE;
		Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, ReimportNavigation, TransactionIndex);
		if (!Result.bSucceeded) return Result;
		// 11) Writability pre-check + save + FailSaveWithRollback.
		return SaveReimportedAsset(
			Session, TargetPackage, WBP, GenerationOutput, ReimportNavigation, TransactionIndex, Result);
	}
	FDesignReimportResult ApplyPreparedReimport(
		const FReimportSession& Session, UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesign2UMGGenerationOutput& GenerationOutput,
		FUMGPrototypeNavigationData& ReimportNavigation, int32& TransactionIndex)
	{
		FDesignReimportResult Result;
		Result.Commit.AssetPath = WBP->GetPathName();
		const auto& Data = Session.Data;
		const auto& GenerationSettings = Session.GenerationSettings;
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
		TransactionIndex = INDEX_NONE;
		TSet<UObject*> ExistingObjects;
		const auto FailApply = [&]()
		{
			FString Note = Result.Commit.Memory == EDesign2UMGMemoryState::Unchanged
				? TEXT("Preparation failed before mutation; asset and manifest unchanged")
				: TEXT("Reimport not saved; no reimport-owned transaction to undo. Earlier in-memory changes remain for the caller to recover");
			if (TransactionIndex != INDEX_NONE)
			{
				const FGuid TransactionId = GUndo ? GUndo->GetContext().TransactionId : FGuid();
				GEditor->EndTransaction();
				TransactionIndex = INDEX_NONE;
				bool bRestored = GeneratedAssetLifecycle::UndoOwnedTransaction(TransactionId);
				if (bRestored)
				{
					Result.Diagnostics.RemoveAll([](const FDesign2UMGMappingDiagnostic& Diagnostic)
					{
						return Diagnostic.Kind == ReimportDiagnosticKinds::Added || Diagnostic.Kind == ReimportDiagnosticKinds::Removed
							|| Diagnostic.Kind == ReimportDiagnosticKinds::KindChanged || Diagnostic.Kind == ReimportDiagnosticKinds::Moved
							|| Diagnostic.Kind == ReimportDiagnosticKinds::Updated;
					});
					bRestored = DiscardDetachedReimportWidgets(WBP->WidgetTree, ExistingObjects, Result.Diagnostics);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
					FKismetEditorUtilities::CompileBlueprint(WBP,
						EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipFiBSearchMetaUpdate);
					bRestored &= WBP->Status == BS_UpToDate || WBP->Status == BS_UpToDateWithWarnings;
				}
				Result.Commit.Memory = bRestored ? EDesign2UMGMemoryState::Restored : EDesign2UMGMemoryState::RecoveryFailed;
				if (!bRestored) Result.Commit.Status = EDesign2UMGCommitStatus::RollbackIncomplete;
				Note = bRestored ? TEXT("Reimport transaction rolled back; asset and manifest not saved")
					: TEXT("Reimport recovery incomplete (undo, detached object cleanup or compile failed); asset and manifest not saved");
			}
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::ApplyFailed, Note);
			Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
			Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());
			if (Session.Document) DesignImportDiagnosticReport::WriteReimport(*Session.Document, Session.Data,
				WBP->GetOutermost()->GetName(), Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit);
			return Result;
		};
		if (!BuildReimportUpdatePlan(
			GenerationSettings, Data, Manifest, ScopeRoot,
			PreparedScopeRoot, MappingBuildState, InstancesById, Plan, AliveInstanceWidgetNames))
		{
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::PrepareFailed,
				TEXT("Mapped class validation failed; existing asset and manifest preserved"));
			return FailApply();
		}
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		if (TOptional<FDesignReimportResult> Early = CheckReimportStructureViability(
			Session, WBP, Manifest, PreparedScopeRoot, Result, DesignSize))
		{
			return MoveTemp(*Early);
		}

		// 9) Apply the update plan inside an editor transaction.
		TArray<UObject*> Objects;
		GetObjectsWithOuter(WBP->WidgetTree, Objects, false);
		ExistingObjects.Append(Objects);
		Result.Commit.Memory = EDesign2UMGMemoryState::Modified;
		TransactionIndex = BeginReimportApplyTransaction(WBP);

		// Generated prototype navigation wiring is plugin-owned transient content, re-authored by the
		// save (step 11) from the new source. Clear it before the tree updates so the safe-delete /
		// identity-conversion gates ("referenced by blueprint graphs") only ever see user-authored nodes.
		// Mixed generated/user keyboard graphs are preserved; each one gets a conflict diagnostic below.
		TArray<FString> PreservedMixedGraphs;
		Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(WBP, &PreservedMixedGraphs);
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
		Shared.GenerationSettings = &GenerationSettings;
		Shared.OwnedNames = &ManifestOwnedNames;
		Shared.InstancesById = &InstancesById;
		Shared.MappingBuildState = &MappingBuildState;
		Shared.Diagnostics = &Result.Diagnostics;
		Shared.bHasDegradation = &Result.bHasDegradation;
		Shared.UpdatedMappedRecords = &FinalMappedRecords;
		Shared.UpdatedGeneratedRecords = &FinalGeneratedRecords;
		Shared.AliveInstanceWidgetNames = &AliveInstanceWidgetNames;
		MappingBuildState.SetRootWidget(WBP->WidgetTree->RootWidget);
		for (const FString& GraphName : PreservedMixedGraphs)
		{
			AddReimportDegradation(
				Shared, ReimportDiagnosticKinds::PrototypeGraphConflict,
				FString::Printf(
					TEXT("Keyboard override '%s' contains user-authored nodes; graph preserved and keyboard regeneration skipped"),
					*GraphName));
		}

		// 9a)-9d) removed / kind-changed / added / reparent。The mutable cross-stage channels
		// (UpdatedD2UOwned / KeepAsIsWidgetNames / NewlyCreatedWidgets) flow through the stage
		// signatures: 9a/9b delete names out of the owned list and record conflict keeps; 9b/9c
		// register newly created widgets; 9d reparent resolves its targets from that registry.
		if (!ApplyRemovedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames)
			|| !ApplyKindChangedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames, NewlyCreatedWidgets)
			|| !ApplyAddedTopLevelNodes(PreparedScopeRoot, Shared, NewlyCreatedWidgets)
			|| !ApplyReparentPlanEntries(Plan, PreparedScopeRoot, Shared, KeepAsIsWidgetNames, NewlyCreatedWidgets))
		{
			return FailApply();
		}

		// 9d-2) Nested-structure diff pre-scan (the move-candidate pool). Must run after 9c created the
		// new widgets and 9a/9b deleted the stale ones - expressed here by the stage ordering itself:
		// the pool returned here is the input of the 9e refresh and the 9g leftover diagnostics below.
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Shared);

		// 9e)-9g) Property updates / nested-delete leftover diagnostics (step comments live with their functions).
		if (!ApplyMatchedPlanEntries(Plan, Shared, GenerationSettings, NestedMoveCandidates, NewlyCreatedWidgets))
		{
			return FailApply();
		}
		ReportNestedRemovalLeftovers(Shared, NestedMoveCandidates);

		// 9h) Re-author the generated transition animations for the updated tree (name-prefix purge inside
		// keeps regenerates from accumulating); the enriched navigation copy carries the animation names
		// into the save, whose wiring plays them around the page switch.
		ReimportNavigation = Session.Data.PrototypeNavigation();
		UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(WBP, PreparedScopeRoot, ReimportNavigation);

		// Mapping diagnostics exit (aligned with the import path's UMGGenerator assembly): the ini validation
		// diagnostics + this round's BuildState-collected match/fallback diagnostics go into
		// ComponentMappingDiagnostics, written by the report into the componentMapping section; previously the reimport report lacked this exit, hiding config errors like class-load failures.
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// 10) Assemble the post-reimport manifest payload + asset finalization (CDO preview size, variable GUIDs).
		GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames);
		FinalizeReimportedAssetState(WBP, DesignSize);

		Result.bSucceeded = true;
		return Result;
	}

}
