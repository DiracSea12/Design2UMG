// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"
#include "UMGWidgetRenderer.h"
#include "Misc/EngineVersionComparison.h"
#include "GeneratedAssetLifecycle.h"

#include "DesignImportDiagnosticReport.h"
#include "DesignImportPlanning.h"
#include "Design2UMGSettings.h"
#include "UMGSchemaSourceExtensions.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace DesignImportPipeline
{
	namespace
	{
		void IndexDesignTree(
			const FUMGWidgetNode& Node,
			const FUMGWidgetNode* Parent,
			FReimportTreeIndex& Index,
			bool bParentAliasHasSource)
		{
			const FString DesignNodeId = GetDesignNodeId(Node);
			const bool bContinuesAlias = Parent && Parent->Children.Num() == 1
				&& GetDesignNodeId(*Parent) == DesignNodeId;
			const bool bOwnsSource = OwnsSourceIdentity(Node);
			if (!DesignNodeId.IsEmpty())
			{
				if (const FUMGWidgetNode** Existing = Index.DesignNodesById.Find(DesignNodeId))
				{
					// Keep the outer carrier, but an entire alias chain may represent only one source node.
					if (!bContinuesAlias || (bParentAliasHasSource && bOwnsSource))
					{
						*Existing = nullptr;
						Index.AmbiguousDesignNodeIds.Add(DesignNodeId);
					}
				}
				else
				{
					Index.DesignNodesById.Add(DesignNodeId, &Node);
				}
			}
			if (Parent)
			{
				Index.ParentsByNode.Add(&Node, Parent);
			}
			// The project widget supplies a mapped instance's implementation; those design descendants are not built.
			if (!Node.Source().MappedComponentKey.IsEmpty()) return;
			const bool bAliasHasSource = bOwnsSource || (bContinuesAlias && bParentAliasHasSource);
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				IndexDesignTree(Child, &Node, Index, bAliasHasSource);
			}
		}

		void BuildDesignTreeIndex(
			const FUMGWidgetNode& Root,
			FReimportTreeIndex& Index)
		{
			Index.DesignNodesById.Reset();
			Index.AmbiguousDesignNodeIds.Reset();
			Index.ParentsByNode.Reset();
			IndexDesignTree(Root, nullptr, Index, false);
		}

		bool ValidateReimportMappedNodes(const FUMGWidgetNode& Node, const FReimportTreeIndex& Index,
			FDesign2UMGComponentMappingBuildState& BuildState, TSet<FString>& CheckedKeys, FString& OutFailureReason)
		{
			if (Node.HasSourceData() && !Node.Source().MappedComponentKey.IsEmpty())
			{
				if (Index.AmbiguousDesignNodeIds.Contains(GetDesignNodeId(Node)))
				{
					OutFailureReason = TEXT("Ambiguous stable design identity for mapped instance: ") + GetDesignNodeId(Node);
					return false;
				}
				const FString& Key = Node.Source().MappedComponentKey;
				if (CheckedKeys.Contains(Key)) return true;
				CheckedKeys.Add(Key);
				const auto* Entry = BuildState.FindEntryForKey(Key);
				if (!Entry || !FSoftClassPath(Entry->WidgetClass).TryLoadClass<UUserWidget>())
				{
					OutFailureReason = TEXT("Mapped class validation failed; existing asset and manifest preserved");
					BuildState.AddDiagnostic(TEXT("mapping_class_unavailable"), FString::Printf(
						TEXT("Mapping '%s' for node '%s' cannot load class '%s'; reimport left the existing asset unchanged"),
						*Key, *Node.Name, Entry ? *Entry->WidgetClass : TEXT("(missing entry)")));
					return false;
				}
				return true; // A mapped instance owns its implementation; its design children are not built.
			}
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				if (!ValidateReimportMappedNodes(Child, Index, BuildState, CheckedKeys, OutFailureReason)) return false;
			}
			return true;
		}

	}

	// ---------------------------------------------------------------------------
	// Reimport stage functions: the main flow (Reimport() below) orchestrates them in steps 1)-11)
	// ---------------------------------------------------------------------------

	/** Steps 1)-3): settings snapshot / re-parse the source / locate the target asset + read the manifest; failure exits early as a full conflict. */
	TOptional<FDesignReimportResult> ResolveReimportSourceAndManifest(
		FDesignDocument& Document,
		const FDesignReimportRequest& Request,
		FDesignReimportResult& Result,
		FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& Data,
		FString& TargetPackage,
		UWidgetBlueprint*& WBP,
		FDesign2UMGGenerationManifestSidecar& Manifest)
	{
		// Resolve the settings snapshot from the same origin as the import path so ini validation diagnostics travel with it.
		SettingsSnapshot =
			Request.SettingsSnapshot.IsSet()
				? Request.SettingsSnapshot.GetValue()
				: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		const FReimportSession Session{ &Document, Request, Data, SettingsSnapshot.GenerationSettings, nullptr, &Result.ImageResult };

		// Re-parse images and convert the source into the current intermediate representation.
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = true;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult AnalysisResult = DesignImportPlanning::AnalyzeImport(Document, AnalysisInput);
		Result.ImageResult = AnalysisResult.ImageResult;
		Result.bHasDegradation = Result.ImageResult.HasFailures();
		if (!AnalysisResult.ErrorMessage.IsEmpty())
		{
			AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_font_preparation_failed"), AnalysisResult.ErrorMessage);
			Result.ComponentMappingDiagnostics = SettingsSnapshot.GenerationSettings.ComponentMapping.ValidationDiagnostics;
			DesignImportDiagnosticReport::WriteReimport(Document, Data,
				FPackageName::ObjectPathToPackageName(Request.TargetAssetPath), Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
			return Result;
		}
		Data = AnalysisResult.Data;

		// Locate the target asset and read its manifest.
		TargetPackage = FPackageName::ObjectPathToPackageName(Request.TargetAssetPath);
		WBP = LoadObject<UWidgetBlueprint>(
			nullptr, *(TargetPackage + TEXT(".") + FPaths::GetBaseFilename(TargetPackage)));
		if (!WBP)
		{
			return HandleFullConflict(
				FString::Printf(TEXT("target asset failed to load: %s"), *TargetPackage),
				Session, MoveTemp(Result.Diagnostics));
		}

		FString ManifestError;
		if (!Design2UMGGenerationManifest::TryLoadSidecarForAsset(TargetPackage, Manifest, ManifestError))
		{
			return HandleFullConflict(ManifestError, Session, MoveTemp(Result.Diagnostics));
		}
		return {};
	}

	/** Validate the document domain, apply mapping on an IR copy, then locate the active reimport scope. */
	TOptional<FDesignReimportResult> PrepareReimportScope(
		const FReimportSession& Session,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesignReimportResult& Result,
		FUMGWidgetNode& PreparedRoot,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		const FUMGWidgetNode*& ScopeRoot,
		bool& bPrototypeHostContinued)
	{
		const FUMGWidgetData& Data = Session.Data;
		// A provider or document-scope mismatch makes the asset relationship ambiguous and produces a full conflict.
		const FString NewProvider = Data.HasSourceDocumentData() ? Data.SourceDocument().Provider : FString();
		const FString NewScope = Data.HasSourceDocumentData() ? Data.SourceDocument().DocumentScope : FString();
		if (!NewProvider.Equals(Manifest.Provider, ESearchCase::IgnoreCase)
			|| !NewScope.Equals(Manifest.DocumentScope, ESearchCase::IgnoreCase))
		{
			return HandleFullConflict(
				FString::Printf(
					TEXT("document scope mismatch: manifest provider='%s' scope='%s', new source provider='%s' scope='%s'"),
					*Manifest.Provider, *Manifest.DocumentScope, *NewProvider, *NewScope),
				Session, MoveTemp(Result.Diagnostics));
		}

		// Establish mapping boundaries before either root lookup or instance reconciliation. The source stays untouched.
		PreparedRoot = Data.RootWidget;
		Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
			PreparedRoot, Session.GenerationSettings.ComponentMapping, Data, MappingBuildState);

		// Locate the manifest source root. An empty identity selects the full IR root;
		// only multi-page prototype hosts carry a page-set fingerprint.
		FReimportTreeIndex SourceIndex;
		BuildDesignTreeIndex(PreparedRoot, SourceIndex);
		if (!Manifest.SourceRootNodeId.IsEmpty()
			&& SourceIndex.AmbiguousDesignNodeIds.Contains(Manifest.SourceRootNodeId))
		{
			return HandleFullConflict(
				FString::Printf(
					TEXT("ambiguous stable design identity '%s' for the recorded source root inside document scope '%s'"),
					*Manifest.SourceRootNodeId, *NewScope),
				Session, MoveTemp(Result.Diagnostics));
		}
		ScopeRoot = Manifest.SourceRootNodeId.IsEmpty()
			? &PreparedRoot
			: SourceIndex.DesignNodesById.FindRef(Manifest.SourceRootNodeId);
		// The switcher root ID fingerprints its page set, so page edits can change it without
		// changing document identity. Continue only between switcher-body roots in the matched
		// document scope; overlay-host changes and single-page downgrades remain conflicts.
		bPrototypeHostContinued = !ScopeRoot
			&& Manifest.SourceRootNodeId.StartsWith(Design2UMGPrototypeTokens::SwitcherRootPrefix())
			&& !Manifest.SourceRootNodeId.EndsWith(Design2UMGPrototypeTokens::OverlayRootSuffix())
			&& Data.HasPrototypeNavigationData()
			&& Data.PrototypeNavigation().IsValid()
			&& Data.PrototypeNavigation().SwitcherWidgetId == GetDesignNodeId(Data.RootWidget);
		if (bPrototypeHostContinued)
		{
			ScopeRoot = &PreparedRoot;
			AddReimportDiagnostic(
				Result.Diagnostics, ReimportDiagnosticKinds::PrototypeHostContinued,
				FString::Printf(
					TEXT("Prototype host root identity renewed: recorded '%s' not found in the new source (page-set content fingerprint changed); provider/scope still match and the new source remains a multi-page prototype, continuing on the new host root '%s'"),
					*Manifest.SourceRootNodeId, *GetDesignNodeId(Data.RootWidget)));
		}
		if (!ScopeRoot)
		{
			return HandleFullConflict(
				FString::Printf(
					TEXT("recorded source root '%s' (%s) not found in the new source"),
					*Manifest.SourceRootName, *Manifest.SourceRootNodeId),
				Session, MoveTemp(Result.Diagnostics));
		}

		if (ScopeRoot != &PreparedRoot)
		{
			// Only the selected subtree is materialized. Discard mapping state and diagnostics from other roots.
			FUMGWidgetNode SelectedRoot = *ScopeRoot;
			PreparedRoot = MoveTemp(SelectedRoot);
			ScopeRoot = &PreparedRoot;
			MappingBuildState = FDesign2UMGComponentMappingBuildState(Session.GenerationSettings.ComponentMapping);
			Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
				PreparedRoot, Session.GenerationSettings.ComponentMapping, Data, MappingBuildState);
		}

		AddReimportDiagnostic(
			Result.Diagnostics, ReimportDiagnosticKinds::ScopeMatched,
			FString::Printf(
				TEXT("Source matched: provider=%s documentScope=%s sourceRoot=%s"),
				*Manifest.Provider, *Manifest.DocumentScope, *Manifest.SourceRootNodeId));
		return {};
	}

	/** Build the update plan over the already mapped, immutable reimport scope. */
	bool BuildReimportUpdatePlan(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportTreeIndex& TreeIndex,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById,
		TArray<FReimportPlanEntry>& Plan,
		TSet<FString>& AliveInstanceWidgetNames,
		FString& OutFailureReason)
	{
		// Build the update plan from one scoped identity index rather than repeated manifest-record tree searches.
		BuildDesignTreeIndex(PreparedScopeRoot, TreeIndex);

		TSet<FString> CheckedMappingKeys;
		if (!ValidateReimportMappedNodes(PreparedScopeRoot, TreeIndex, MappingBuildState, CheckedMappingKeys, OutFailureReason))
			return false;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			if (TreeIndex.AmbiguousDesignNodeIds.Contains(Record.DesignNodeId))
			{
				OutFailureReason = FString::Printf(
					TEXT("Ambiguous stable design identity '%s' participates in reimport reconciliation; existing asset and manifest preserved"),
					*Record.DesignNodeId);
				return false;
			}
			if (InstancesById.Contains(Record.DesignNodeId))
			{
				OutFailureReason = FString::Printf(
					TEXT("Manifest contains duplicate design identity '%s'; existing asset and manifest preserved"),
					*Record.DesignNodeId);
				return false;
			}
			InstancesById.Add(Record.DesignNodeId, &Record);

			FReimportPlanEntry Entry;
			Entry.ManifestRecord = &Record;
			Entry.NewNode = TreeIndex.DesignNodesById.FindRef(Record.DesignNodeId);
			Entry.bNewKindMapped = Entry.NewNode && Entry.NewNode->HasSourceData()
				&& !Entry.NewNode->Source().MappedComponentKey.IsEmpty();

			if (!Entry.NewNode)
			{
				Entry.Action = EReimportPlanAction::Removed;
			}
			else if (Entry.bNewKindMapped != (Record.Kind == Design2UMGManifestKinds::Mapped))
			{
				Entry.Action = EReimportPlanAction::KindChanged;
			}
			else
			{
				Entry.Action = EReimportPlanAction::Matched;
			}
			Plan.Add(Entry);
		}

		// Expanded component descendants may legitimately repeat definition ids across distinct instances. New
		// top-level roots, however, become reconciliation identities for this operation and must be unique before mutation.
		if (!InstancesById.Contains(GetDesignNodeId(PreparedScopeRoot)))
		{
			for (const FUMGWidgetNode& Child : PreparedScopeRoot.Children)
			{
				const FString ChildId = GetDesignNodeId(Child);
				if (!InstancesById.Contains(ChildId) && TreeIndex.AmbiguousDesignNodeIds.Contains(ChildId))
				{
					OutFailureReason = FString::Printf(
						TEXT("Ambiguous stable design identity '%s' would become a new top-level reimport instance; existing asset and manifest preserved"),
						*ChildId);
					return false;
				}
			}
		}

		// Definition descendants can repeat across component instances. They are not global reconciliation keys;
		// nested refresh still resolves within its parent. Make that exclusion observable instead of picking a branch.
		TArray<FString> LocalIds = TreeIndex.AmbiguousDesignNodeIds.Array();
		LocalIds.Sort();
		for (const FString& Id : LocalIds)
		{
			MappingBuildState.AddDiagnostic(TEXT("reimport_ambiguous_local_identity"), FString::Printf(
				TEXT("Repeated nested stable identity '%s' excluded from document-wide lookup; descendants remain parent-local"), *Id));
		}

		// Matched and type-changed instances remain in the source design; preserve them during subtree deletion.
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Removed)
			{
				AliveInstanceWidgetNames.Add(Entry.ManifestRecord->WidgetName);
			}
		}
		return true;
	}

	/** Validate target-tree structure before mutation; failure exits early as a full conflict. */
	TOptional<FDesignReimportResult> CheckReimportStructureViability(
		const FReimportSession& Session,
		UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FReimportTreeIndex& TreeIndex,
		FDesignReimportResult& Result,
		FIntPoint& DesignSize)
	{
		// A missing target tree or absence of every manifest instance indicates a project-side restructure.
		if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
		{
			return HandleFullConflict(
				TEXT("target widget tree is missing a root widget"),
				Session, MoveTemp(Result.Diagnostics));
		}
		RebuildReimportWidgetNameIndex(WBP->WidgetTree, TreeIndex);
		int32 LocatedInstances = 0;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			LocatedInstances += TreeIndex.WidgetsByName.Contains(FName(*Record.WidgetName)) ? 1 : 0;
		}
		if (LocatedInstances == 0)
		{
			return HandleFullConflict(
				TEXT("none of the recorded instances could be located in the target widget tree (project restructure suspected)"),
				Session, MoveTemp(Result.Diagnostics));
		}

		DesignSize = FIntPoint(
			FMath::CeilToInt(PreparedScopeRoot.Bounds.W),
			FMath::CeilToInt(PreparedScopeRoot.Bounds.H));
		if (DesignSize.X <= 0 || DesignSize.Y <= 0)
		{
			return HandleFullConflict(
				TEXT("new source did not provide a valid root design size"),
				Session, MoveTemp(Result.Diagnostics));
		}
		return {};
	}

	void RebuildReimportWidgetNameIndex(UWidgetTree* WidgetTree, FReimportTreeIndex& TreeIndex)
	{
		TreeIndex.WidgetsByName.Reset();
		if (!WidgetTree)
		{
			return;
		}
		WidgetTree->ForEachWidget([&TreeIndex](UWidget* Widget)
		{
			TreeIndex.WidgetsByName.Add(Widget->GetFName(), Widget);
		});
	}

	/** Open the editor transaction and mark the existing tree transactional; INDEX_NONE means no owned transaction. */
	int32 BeginReimportApplyTransaction(UWidgetBlueprint* WBP)
	{
		// Note: FScopedTransaction::Cancel only discards the transaction record, it does not roll objects back; a real
		// rollback requires an explicit Undo after End. Bare Begin/End is used here so the save-failure path can
		// restore the asset's original state. Nesting defense: editor transactions nest by ref-count - if an outer
		// transaction is already active, the one we Begin is not an independent undo entry (End only decrements the
		// count), and UndoTransaction after End would undo an unrelated user transaction further down the undo stack.
		// So no own transaction is opened while an outer transaction is active (Modify snapshots still enter the outer transaction, undone by it as a whole).
		const bool bCanOwnTransaction = GEditor && GEditor->CanTransact() && !GEditor->IsTransactionActive();
		const int32 TransactionIndex = bCanOwnTransaction
			? GEditor->BeginTransaction(
				TEXT("Design2UMG Reimport"),
				NSLOCTEXT("Design2UMG", "SafeReimportTransaction", "Design2UMG Safe Reimport"),
				WBP)
			: INDEX_NONE;

		// Widgets built by the generation chain lack RF_Transactional by default; the transaction captures no Modify
		// snapshot, so a save-failure rollback could not restore properties (the UMG editor's own delete/edit flows add the flag explicitly, same as here).
		if (WBP->WidgetTree)
		{
			WBP->WidgetTree->SetFlags(RF_Transactional);
			WBP->WidgetTree->ForEachWidget([](UWidget* TreeWidget)
			{
				TreeWidget->SetFlags(RF_Transactional);
			});
		}
		return TransactionIndex;
	}

	/** Assemble the post-reimport manifest payload, including preview-size refresh and GUID backfill. */
	FDesign2UMGGenerationOutput BuildReimportManifestPayload(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		bool bPrototypeHostContinued,
		const FUMGWidgetData& Data,
		const FReimportApplySharedState& Shared,
		TSet<FString>& UpdatedD2UOwned,
		const TArray<FString>& KeepAsIsWidgetNames)
	{
		// Assemble the post-reimport manifest payload.
		FDesign2UMGGenerationOutput GenerationOutput;
		GenerationOutput.Manifest.Provider = Manifest.Provider;
		GenerationOutput.Manifest.DocumentScope = Manifest.DocumentScope;
		// Under host continuation the source-root identity is refreshed from the new source (the new page-set
		// fingerprint), so the next reimport benchmarks against it; the recording rule matches the import side:
		GenerationOutput.Manifest.SourceRootNodeId = bPrototypeHostContinued
			? GetDesignNodeId(Data.RootWidget)
			: Manifest.SourceRootNodeId;
		GenerationOutput.Manifest.SourceRootName = bPrototypeHostContinued
			? Data.RootWidget.Name
			: Manifest.SourceRootName;
		GenerationOutput.Manifest.MappedInstances = MoveTemp(*Shared.UpdatedMappedRecords);
		GenerationOutput.Manifest.TopLevelGenerated = MoveTemp(*Shared.UpdatedGeneratedRecords);

		// Instances kept as-is (conflicts/location failures): backfill their original records into the new manifest.
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			if (!KeepAsIsWidgetNames.Contains(Record.WidgetName))
			{
				continue;
			}
			if (Record.Kind == Design2UMGManifestKinds::Mapped)
			{
				GenerationOutput.Manifest.MappedInstances.Add(ManifestRecordToMappedRecord(Record));
			}
			else
			{
				FDesign2UMGGeneratedInstanceRecord Kept;
				Kept.DesignNodeId = Record.DesignNodeId;
				Kept.WidgetName = Record.WidgetName;
				GenerationOutput.Manifest.TopLevelGenerated.Add(MoveTemp(Kept));
			}
		}

		// Instances created by this generation run (top-level additions, kind changes, and nested mapped nodes) enter the new manifest.
		for (const FDesign2UMGMappedInstanceRecord& NewMapped : Shared.MappingBuildState->GetMappedInstances())
		{
			if (!GenerationOutput.Manifest.MappedInstances.ContainsByPredicate(
				[&NewMapped](const FDesign2UMGMappedInstanceRecord& M) { return M.WidgetName == NewMapped.WidgetName; }))
			{
				GenerationOutput.Manifest.MappedInstances.Add(NewMapped);
			}
		}
		for (const FDesign2UMGGeneratedInstanceRecord& NewGenerated : Shared.MappingBuildState->GetTopLevelGenerated())
		{
			if (!GenerationOutput.Manifest.TopLevelGenerated.ContainsByPredicate(
				[&NewGenerated](const FDesign2UMGGeneratedInstanceRecord& G) { return G.WidgetName == NewGenerated.WidgetName; }))
			{
				GenerationOutput.Manifest.TopLevelGenerated.Add(NewGenerated);
			}
		}

		// New d2uOwned = (old set - deleted names) union names created by this run; then drop names absent from the tree.
		for (const FString& CreatedName : Shared.MappingBuildState->GetCreatedWidgetNames())
		{
			UpdatedD2UOwned.Add(CreatedName);
		}
		if (Shared.WBP->WidgetTree)
		{
			TSet<FName> LiveNames;
			Shared.WBP->WidgetTree->ForEachWidget([&LiveNames](UWidget* Widget)
			{
				LiveNames.Add(Widget->GetFName());
			});
			for (auto It = UpdatedD2UOwned.CreateIterator(); It; ++It)
			{
				if (!LiveNames.Contains(FName(**It)))
				{
					It.RemoveCurrent();
				}
			}
		}
		GenerationOutput.Manifest.D2UOwnedNames = UpdatedD2UOwned.Array();
		GenerationOutput.Manifest.D2UOwnedNames.Sort();

		return GenerationOutput;
	}

	/**
	 * Asset finalization covers side effects unrelated to manifest assembly:
	 * refresh the CDO preview size when the design root size changed (written back by SaveWidgetBlueprint after compiling);
	 * backfill missing variable GUIDs of generation-chain-created widgets with the compiler's own deterministic rule.
	 */
	void EnsureReimportWidgetGuids(UWidgetBlueprint* WBP)
	{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		// Widgets created by this generation run (plus test injections via ConstructWidget) have no
		// variable GUID, and the compiler ensures on missing entries. Backfill with the compiler's own deterministic
		// rule; existing GUIDs stay untouched. Modify first to capture the GUID map's pre-transaction state: on a
		// save-failure rollback the map restores with the asset, or the post-rollback recompile would trip the compiler's ensure on a "deleted widget's leftover GUID".
		if (WBP->WidgetTree)
		{
			WBP->Modify();
			WBP->ForEachSourceWidget([WBP](UWidget* TreeWidget)
			{
				const FName WidgetName = TreeWidget->GetFName();
				if (!WBP->WidgetVariableNameToGuidMap.Contains(WidgetName))
				{
					WBP->WidgetVariableNameToGuidMap.Add(
						WidgetName, FGuid::NewDeterministicGuid(TreeWidget->GetPathName()));
				}
			});
		}

#endif
	}

	void FinalizeReimportedAssetState(UWidgetBlueprint* WBP, FIntPoint DesignSize, const TOptional<FBox2D>& ExportBounds)
	{
		if (UUserWidget* CDO = Cast<UUserWidget>(
			WBP->GeneratedClass ? WBP->GeneratedClass->GetDefaultObject() : nullptr))
		{
			// The CDO lacks RF_Transactional by default (the WidgetTree got it above; same reasoning here):
			// without the flag, Modify never enters the transaction buffer, and a save-failure rollback / user undo would leave the preview size at the new value.
			CDO->SetFlags(RF_Transactional);
			CDO->Modify();
			CDO->DesignTimeSize = FVector2D(DesignSize.X, DesignSize.Y);
			CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		}

		FUMGWidgetRenderer::SetBlueprintExportBounds(WBP, ExportBounds);
		EnsureReimportWidgetGuids(WBP);

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		// Generated transition animations follow the same rule as widgets: a non-empty GUID map makes the
		// compiler ensure on animations missing from it, so backfill them deterministically too (same
		// formula as the compiler's own empty-map path, keeping GUIDs stable across regenerates).
		WBP->Modify();
		for (const TObjectPtr<UWidgetAnimation>& Animation : WBP->Animations)
		{
			if (Animation && !WBP->WidgetVariableNameToGuidMap.Contains(Animation->GetFName()))
			{
				WBP->WidgetVariableNameToGuidMap.Add(
					Animation->GetFName(), FGuid::NewDeterministicGuid(Animation->GetPathName()));
			}
		}
#endif
	}

	/** Validate writability, compile and save, then roll back the transaction on failure. */
	FDesignReimportResult SaveReimportedAsset(
		const FReimportSession& Session,
		const FString& TargetPackage,
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput& GenerationOutput,
		const FUMGPrototypeNavigationData& PrototypeNavigation,
		int32 TransactionIndex,
		FDesignReimportResult& Result)
	{
		const auto WriteReport = [&]()
		{
			if (Session.Document) DesignImportDiagnosticReport::WriteReimport(
				*Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics, &Result.Commit, &Result.ImageResult);
		};
		const auto FailSaveWithRollback = [&](const FString& Reason)
		{
			FString RollbackNote = TEXT("; no reimport-owned editor transaction; in-memory changes remain for the caller to recover");
			if (TransactionIndex != INDEX_NONE)
			{
				const FGuid TransactionId = GUndo ? GUndo->GetContext().TransactionId : FGuid();
				GEditor->EndTransaction();
				TransactionIndex = INDEX_NONE;
				bool bRestored = GeneratedAssetLifecycle::UndoOwnedTransaction(TransactionId);
				if (bRestored)
				{
					// The generated class is not restored by the template-tree transaction.
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
					FKismetEditorUtilities::CompileBlueprint(WBP,
						EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipFiBSearchMetaUpdate);
					bRestored = WBP->Status == BS_UpToDate || WBP->Status == BS_UpToDateWithWarnings;
				}
				Result.Commit.Memory = bRestored ? EDesign2UMGMemoryState::Restored : EDesign2UMGMemoryState::RecoveryFailed;
				if (!bRestored) Result.Commit.Status = EDesign2UMGCommitStatus::RollbackIncomplete;
				RollbackNote = bRestored
					? TEXT("; transaction rolled back, asset keeps its pre-reimport state in memory")
					: TEXT("; transaction undo or restored blueprint compile failed; memory recovery incomplete");
			}
			RollbackNote += Result.Commit.Asset == EDesign2UMGFileState::Indeterminate
				? TEXT("; asset file state is indeterminate, manifest not updated")
				: TEXT("; asset file unchanged, manifest not updated");
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::SaveFailed, Reason + RollbackNote);
			Result.bSucceeded = false;
			WriteReport();
		};

		// 11a) Pre-check: do not enter the save flow when the target file is not writable. The uasset checks only its
		// read-only attribute (a write probe would be falsely killed by the engine's read handle on the in-process
		// loaded state; see IsFileWritableForSave); the sidecar is our own JSON that the engine never loads, so no
		// read-handle false positives exist - keep the read-only + write-probe double check to prevent the half-update
		// "uasset updated + sidecar write failed". Perforce-unchecked-out target scenario: a read-only uasset makes the
		// engine's SavePackage hit a fatal appError (the editor crashes outright; no product-grade diagnostic possible). Intercept it: the save never happens (the disk stays as-is).
		{
			FString WritabilityBlockers;
			FString Reason;
			if (!GeneratedAssetLifecycle::IsFileWritableForSave(
				FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension()),
				/*bAllowWriteProbe=*/false, Reason))
			{
				WritabilityBlockers = Reason;
			}
			if (!GeneratedAssetLifecycle::IsFileWritableForSave(
				Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage),
				/*bAllowWriteProbe=*/true, Reason))
			{
				if (!WritabilityBlockers.IsEmpty())
				{
					WritabilityBlockers += TEXT("; ");
				}
				WritabilityBlockers += Reason;
			}
			if (!WritabilityBlockers.IsEmpty())
			{
				FailSaveWithRollback(FString::Printf(
					TEXT("Pre-save writability check failed (%s); save not attempted"),
					*WritabilityBlockers));
				return Result;
			}
		}

		Result.Commit = FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput, &PrototypeNavigation);
		Result.bHasDegradation |= GenerationOutput.bHasDegradation || !Session.Data.Warnings.IsEmpty() || Result.ImageResult.HasFailures();
		if (Result.Commit.Status == EDesign2UMGCommitStatus::Succeeded && Result.bHasDegradation)
			Result.Commit.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
		Result.Diagnostics.Append(GenerationOutput.MappingDiagnostics);
		if (Result.Commit.Asset != EDesign2UMGFileState::Saved)
		{
			FailSaveWithRollback(FString::Printf(TEXT("Save/compile failed for %s"), *TargetPackage));
			return Result;
		}

		// Once the asset is saved, do not undo memory into a state that disagrees with disk.
		if (TransactionIndex != INDEX_NONE) GEditor->EndTransaction();
		Result.UpdatedBlueprint = WBP;
		Result.bSucceeded = Result.Commit.IsSuccess();
		if (Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure)
		{
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::SaveFailed,
				FString::Printf(TEXT("Asset %s was saved but its generation manifest could not be written; the asset and the manifest may be inconsistent"),
					*TargetPackage));
		}
		else
		{
			AddReimportDiagnostic(Result.Diagnostics, ReimportDiagnosticKinds::Completed,
				FString::Printf(TEXT("Reimport of %s applied%s (manifest updated)"), *TargetPackage,
					Result.Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation ? TEXT(" with degradation") : TEXT("")));
		}
		WriteReport();
		return Result;
	}
}
