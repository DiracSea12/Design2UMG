// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"
#include "GeneratedAssetLifecycle.h"
#include "DesignImageResourceLimits.h"
#include "PreBuildResourceNormalizer.h"
#include "Design2UMGEffectMaterialBuilder.h"

#include "Design2UMGPrototypeGraphWiring.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "Design2UMGComponentBlueprints.h"
#include "DesignImportDiagnosticReport.h"
#include "Misc/PackageName.h"
#include "Misc/EngineVersionComparison.h"

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

		// Resolve settings, parse the source, and load the target asset with its manifest.
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
		const FReimportSession Session{ &Document, Request, Data, GenerationSettings, nullptr, &Result.ImageResult };
		if (!Manifest.bComponentBulkRoot && !Manifest.GeneratedAssetHash.IsEmpty())
		{
			Result.bFullConflict = true;
			Result.Commit.Status = EDesign2UMGCommitStatus::Conflict;
			AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_component_batch_root_required"),
				TEXT("Select the parent WBP created by the original import to update this component and its dependencies together. This selected asset is a generated child WBP; its saved content was left unchanged."));
			DesignImportDiagnosticReport::WriteReimport(Document, Data, TargetPackage, Result.Diagnostics, nullptr, &Result.Commit, &Result.ImageResult);
			return Result;
		}
		if (Manifest.bComponentBulkRoot)
		{
			if (Data.SourceDocument().ComponentDependenciesJson.IsEmpty())
			{
				Result.bFullConflict = true;
				Result.Commit.Status = EDesign2UMGCommitStatus::Conflict;
				AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_component_dependencies_missing"),
					TEXT("This asset owns a component batch; the new source is missing its dependency bundle"));
				DesignImportDiagnosticReport::WriteReimport(Document, Data, TargetPackage, Result.Diagnostics, nullptr, &Result.Commit, &Result.ImageResult);
				return Result;
			}
			FDesign2UMGGenerationOutput Output;
			FUMGPrototypeNavigationData Navigation;
			UWidgetBlueprint* Updated = FDesign2UMGComponentBlueprintContext::Build(Data,
				FPackageName::GetLongPackagePath(TargetPackage), &GenerationSettings, &Output, &Navigation, WBP);
			Result.Commit = Updated ? FUMGGenerator::SaveWidgetBlueprint(Updated, &Output, &Navigation) : Output.Commit;
			Result.bHasDegradation = Output.bHasDegradation || !Data.Warnings.IsEmpty() || Result.ImageResult.HasFailures();
			if (Result.Commit.Status == EDesign2UMGCommitStatus::Succeeded && Result.bHasDegradation)
				Result.Commit.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
			Result.bSucceeded = Result.Commit.IsSuccess();
			Result.UpdatedBlueprint = Result.Commit.Asset == EDesign2UMGFileState::Saved ? Updated : nullptr;
			// Bulk generation also carries the page ownership merge events. Route those
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
			DesignImportDiagnosticReport::WriteReimport(Document, Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
			return Result;
		}

		FDesign2UMGGenerationOutput GenerationOutput;
		FUMGPrototypeNavigationData ReimportNavigation;
		int32 TransactionIndex = INDEX_NONE;
		Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, ReimportNavigation, TransactionIndex);
		if (!Result.bSucceeded) return Result;
		// Validate writability, save, and roll back all mutations on failure.
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
		if (Session.ImageResult) Result.ImageResult = *Session.ImageResult;
		Result.bHasDegradation = Result.ImageResult.HasFailures();
		const auto& Data = Session.Data;
		const auto& GenerationSettings = Session.GenerationSettings;
		// Match the document domain and prepare the active scope with its local mapping state.
		FUMGWidgetNode PreparedRoot;
		FDesign2UMGComponentMappingBuildState MappingBuildState(GenerationSettings.ComponentMapping);
		const FUMGWidgetNode* ScopeRoot = nullptr;
		bool bPrototypeHostContinued = false;
		if (TOptional<FDesignReimportResult> Early = PrepareReimportScope(
			Session, Manifest, Result, PreparedRoot, MappingBuildState, ScopeRoot, bPrototypeHostContinued))
		{
			return MoveTemp(*Early);
		}

		// Build the update plan and validate the immutable prepared tree.
		const FUMGWidgetNode& PreparedScopeRoot = *ScopeRoot;
		FReimportTreeIndex TreeIndex;
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*> InstancesById;
		TArray<FReimportPlanEntry> Plan;
		TSet<FString> AliveInstanceWidgetNames;
		TransactionIndex = INDEX_NONE;
		TSet<UObject*> ExistingObjects;
		DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
		DesignImageResources::FImageOperationBudget* Budget = Session.Budget ? Session.Budget : &LocalBudget;
		const auto FailApply = [&]()
		{
			const FString& ResourceError = Budget->GetRefusalError();
			if (!ResourceError.IsEmpty() && !Result.Diagnostics.ContainsByPredicate(
				[&](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Message.Contains(ResourceError); }))
			{
				AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::PrepareFailed, ResourceError);
			}
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
				WBP->GetOutermost()->GetName(), Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
			return Result;
		};
		FString PrepareFailureReason;
		if (!BuildReimportUpdatePlan(
			Manifest, PreparedScopeRoot, TreeIndex, MappingBuildState, InstancesById, Plan, AliveInstanceWidgetNames,
			PrepareFailureReason))
		{
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::PrepareFailed, PrepareFailureReason);
			return FailApply();
		}
		FIntPoint DesignSize = FIntPoint::ZeroValue;
		if (TOptional<FDesignReimportResult> Early = CheckReimportStructureViability(
			Session, WBP, Manifest, PreparedScopeRoot, TreeIndex, Result, DesignSize))
		{
			return MoveTemp(*Early);
		}

		// Prepare persistent resources before mutating the old tree. Nested builds share this budget.
		const FString PackagePath = FPackageName::GetLongPackagePath(WBP->GetOutermost()->GetName());
		const FString AssetName = WBP->GetName();
		TArray<TStrongObjectPtr<UMaterialInterface>> PreparedMaterials;
		if (!FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(PreparedRoot, PrepareFailureReason)
			|| !Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(PreparedRoot, PrepareFailureReason)
			|| !Design2UMGEffectMaterialBuilder::PrepareRetainerEffectMaterials(PreparedRoot, PreparedMaterials, PrepareFailureReason)
			|| !FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
				PreparedRoot, PackagePath, AssetName, PrepareFailureReason, Budget))
		{
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::PrepareFailed, PrepareFailureReason);
			return FailApply();
		}

		// Apply the update plan inside an editor transaction.
		TArray<UObject*> Objects;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		GetObjectsWithOuter(WBP->WidgetTree, Objects, false);
#else
		GetObjectsWithOuter(WBP->WidgetTree, Objects, EGetObjectsFlags::None);
#endif
		ExistingObjects.Append(Objects);
		Result.Commit.Memory = EDesign2UMGMemoryState::Modified;
		TransactionIndex = BeginReimportApplyTransaction(WBP);

		// Generated prototype navigation wiring is plugin-owned transient content, re-authored by the
		// save from the new source. Clear it before the tree updates so the safe-delete /
		// identity-conversion gates ("referenced by blueprint graphs") only ever see user-authored nodes.
		// Mixed generated/user keyboard graphs are preserved; each one gets a conflict diagnostic below.
		TArray<FString> PreservedMixedGraphs;
		Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(WBP, &PreservedMixedGraphs);
		TArray<FDesign2UMGMappedInstanceRecord> FinalMappedRecords;
		TArray<FDesign2UMGGeneratedInstanceRecord> FinalGeneratedRecords;
		TSet<FString> UpdatedD2UOwned(Manifest.D2UOwned);
		const TSet<FString> ManifestOwnedNames(Manifest.D2UOwned);
		TArray<FString> KeepAsIsWidgetNames;
		// Widgets created by this reimport operation (designNodeId -> widget), used by reparent target resolution:
		// new containers (design-added nodes) are also clearly D2U-owned.
		TMap<FString, UWidget*> NewlyCreatedWidgets;

		FReimportApplySharedState Shared;
		Shared.WBP = WBP;
		Shared.WidgetTree = WBP->WidgetTree;
		Shared.HostPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
		Shared.DesignSize = DesignSize;
		Shared.GenerationSettings = &GenerationSettings;
		Shared.Budget = Budget;
		Shared.OwnedNames = &ManifestOwnedNames;
		Shared.InstancesById = &InstancesById;
		Shared.TreeIndex = &TreeIndex;
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

		// Structural updates share ownership, conflict-preservation and newly created widget records.
		if (!ApplyRemovedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames)
			|| !ApplyKindChangedPlanEntries(Plan, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames, NewlyCreatedWidgets)
			|| !ApplyAddedTopLevelNodes(PreparedScopeRoot, Shared, NewlyCreatedWidgets))
		{
			return FailApply();
		}

		// Rebuild name lookups after structural changes for reparenting and nested-move discovery.
		// Property refresh may replace widgets, so it queries the live tree instead.
		RebuildReimportWidgetNameIndex(WBP->WidgetTree, TreeIndex);
		if (!ApplyReparentPlanEntries(Plan, PreparedScopeRoot, Shared, KeepAsIsWidgetNames, NewlyCreatedWidgets))
		{
			return FailApply();
		}

		// Discover move candidates after additions and removals; refresh consumes them before leftover reporting.
		TArray<UWidget*> NestedMoveCandidates = CollectNestedMoveCandidates(PreparedScopeRoot, Shared);

		if (!ApplyMatchedPlanEntries(Plan, Shared, GenerationSettings, NestedMoveCandidates, NewlyCreatedWidgets))
		{
			return FailApply();
		}
		if (!Budget->GetRefusalError().IsEmpty()) return FailApply();

		ReportNestedRemovalLeftovers(Shared, NestedMoveCandidates);

		// Regenerate transition animations on the updated tree and carry their names into navigation wiring.
		ReimportNavigation = Session.Data.PrototypeNavigation();
		UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(WBP, PreparedScopeRoot, ReimportNavigation);

		// Include mapping configuration and match diagnostics in the reimport report.
		Result.ComponentMappingDiagnostics = GenerationSettings.ComponentMapping.ValidationDiagnostics;
		Result.ComponentMappingDiagnostics.Append(MappingBuildState.GetDiagnostics());

		// Assemble the post-reimport manifest payload and finalize the CDO preview size and variable GUIDs.
		Result.bHasDegradation |= MappingBuildState.HasDegradation() || !Data.Warnings.IsEmpty();
		GenerationOutput = BuildReimportManifestPayload(
			Manifest, bPrototypeHostContinued, Data, Shared, UpdatedD2UOwned, KeepAsIsWidgetNames);
		GenerationOutput.bHasDegradation = Result.bHasDegradation;
		FinalizeReimportedAssetState(WBP, DesignSize, Data.ExportBounds);

		Result.bSucceeded = true;
		return Result;
	}

}
