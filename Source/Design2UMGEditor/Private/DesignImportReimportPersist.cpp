// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "DesignImportDiagnosticReport.h"
#include "DesignImportPlanning.h"
#include "Design2UMGSettings.h"
#include "UMGSchemaSourceExtensions.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace DesignImportPipeline
{
	namespace
	{
		const FUMGWidgetNode* FindNodeByDesignId(const FUMGWidgetNode& Node, const FString& DesignNodeId)
		{
			if (GetDesignNodeId(Node) == DesignNodeId)
			{
				return &Node;
			}
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				if (const FUMGWidgetNode* Found = FindNodeByDesignId(Child, DesignNodeId))
				{
					return Found;
				}
			}
			return nullptr;
		}

		/** Whether the IR node is in the "instantiable mapped form": hit a mapping Key and the mapped class is currently loadable. */
		bool IsMappedKind(
			const FUMGWidgetNode& Node,
			const FDesign2UMGComponentMappingConfig& MappingConfig,
			TMap<FString, bool>& MappedKindCache)
		{
			if (!Node.HasSourceData() || Node.Source().MappedComponentKey.IsEmpty())
			{
				return false;
			}

			const FString& Key = Node.Source().MappedComponentKey;
			if (bool* Cached = MappedKindCache.Find(Key))
			{
				return *Cached;
			}

			bool bMappedKind = false;
			if (const DesignComponentMapping::FComponentMappingEntry* Entry =
				MappingConfig.Table.FindEntryByKey(Key))
			{
				UClass* WidgetClass = FSoftClassPath(Entry->WidgetClass).TryLoadClass<UUserWidget>();
				bMappedKind = WidgetClass != nullptr && WidgetClass->IsChildOf<UUserWidget>();
			}
			MappedKindCache.Add(Key, bMappedKind);
			return bMappedKind;
		}

		/**
		 * Pre-save writability check (the Perforce-unchecked-out target scenario):
		 * file exists and the read-only attribute is set = not writable; a missing file counts as writable (the save
		 * creates it then; there is no predictable read-only state). Interception must happen up front: the engine's
		 * SavePackage on a read-only uasset hits a fatal appError - the editor simply crashes; it cannot become a
		 * diagnostic + rollback inside the product. The write probe ("open in append mode, then close": creates no
		 * file, changes no content, only verifies a write handle is obtainable) is enabled for the sidecar only
		 * (bAllowWriteProbe): once the uasset is loaded from disk by this process (the reimport main path's normal
		 * state), the engine's read handle on the file has no write sharing, so an OpenWrite probe necessarily fails - a false positive would block saves that could succeed (the engine save path itself can write).
		 * Real external locks / IO failures on the uasset are caught by the SavePackage failure path (already in FailSaveWithRollback).
		 */
		bool IsFileWritableForSave(const FString& InPath, bool bAllowWriteProbe, FString& OutReason)
		{
			if (!IFileManager::Get().FileExists(*InPath))
			{
				return true;
			}
			if (IFileManager::Get().IsReadOnly(*InPath))
			{
				OutReason = FString::Printf(TEXT("'%s' is read-only"), *InPath);
				return false;
			}
			if (!bAllowWriteProbe)
			{
				return true;
			}
			IFileHandle* Probe = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(
				*InPath, /*bAppend=*/true, /*bAllowRead=*/false);
			if (!Probe)
			{
				OutReason = FString::Printf(TEXT("write probe failed on '%s'"), *InPath);
				return false;
			}
			delete Probe;
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
		// 1) Resolve the settings snapshot (same origin as the import path: ini validation diagnostics enter the generation chain with the snapshot).
		SettingsSnapshot =
			Request.SettingsSnapshot.IsSet()
				? Request.SettingsSnapshot.GetValue()
				: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		const FReimportSession Session{ Document, Request, Data, SettingsSnapshot.GenerationSettings };

		// 2) Re-parse the source (image parsing + Convert into the new IR).
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = true;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Document, AnalysisInput);
		Data = AnalysisResult.Data;

		// 3) Locate the target asset + read the manifest.
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

	/** Steps 4)-5): DocumentScope matching, source-root location and host continuation; failure exits early as a full conflict. */
	TOptional<FDesignReimportResult> MatchReimportScopeAndRoot(
		const FReimportSession& Session,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		FDesignReimportResult& Result,
		const FUMGWidgetNode*& ScopeRoot,
		bool& bPrototypeHostContinued)
	{
		const FUMGWidgetData& Data = Session.Data;
		// 4) DocumentScope matching: a mismatch of provider or scope = unclear relationship, full conflict.
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

		// 5) Locate the manifest-recorded sourceRoot in the new IR (the page/root-node relation).
		// An empty sourceRoot identity = the synthesized switcher host root of a single-page interactive document (only
		// multi-page documents get a fingerprint token id; see the PrototypePages.Num() > 1 branch of BuildAnalysis) -
		// this is the current identity rule, not legacy-sidecar compatibility: on empty, the new IR root simply is the scope.
		ScopeRoot = Manifest.SourceRootNodeId.IsEmpty()
			? &Data.RootWidget
			: FindNodeByDesignId(Data.RootWidget, Manifest.SourceRootNodeId);
		// Host continuation: the synthesized host root id of a multi-page prototype is a content fingerprint of the page set (BuildPrototypeInternalTokenHash =
		// CRC32 of each page's "id|name;" concatenation); adding/removing pages necessarily rewrites it, but a content fingerprint is not the document identity -
		// provider + documentScope already matched in step 4, and when the new source is itself still a multi-page prototype, this is the same
		// host's page-set evolution, not an "unclear relationship": do not reject on a differing fingerprint; continue the existing structure
		// diff with the new source's actual host root as the scope (added pages go through added, removed pages through safe delete), and record the new fingerprint in the manifest on success.
		// The decision is narrowed to "old and new hosts are both switcher bodies": a record id with the _root suffix = an overlay wrapper root;
		// the new root's design id must be exactly the new source's SwitcherWidgetId (the switcher body's synthesized root). The
		// overlay wrapper root's non-instance children are synthetic layers (switcher/overlay hosts), not pages - admitting them would
		// hang them at the wrong level as top-level additions; a host-form flip (overlay appearing/disappearing) keeps the conservative
		// full-conflict semantics just like the single-page downgrade (the latter is the settled behavior of slice C CASE-011).
		bPrototypeHostContinued = !ScopeRoot
			&& Manifest.SourceRootNodeId.StartsWith(Design2UMGPrototypeTokens::SwitcherRootPrefix())
			&& !Manifest.SourceRootNodeId.EndsWith(Design2UMGPrototypeTokens::OverlayRootSuffix())
			&& Data.HasPrototypeNavigationData()
			&& Data.PrototypeNavigation().IsValid()
			&& Data.PrototypeNavigation().SwitcherWidgetId == GetDesignNodeId(Data.RootWidget);
		if (bPrototypeHostContinued)
		{
			ScopeRoot = &Data.RootWidget;
			AddReimportDiagnostic(
				Result.Diagnostics, TEXT("reimport_prototype_host_continued"),
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

		AddReimportDiagnostic(
			Result.Diagnostics, TEXT("reimport_scope_matched"),
			FString::Printf(
				TEXT("Source matched: provider=%s documentScope=%s sourceRoot=%s"),
				*Manifest.Provider, *Manifest.DocumentScope, *Manifest.SourceRootNodeId));
		return {};
	}

	/** Steps 6)-7): Component Mapping two-tier match (on an IR copy) + update-plan construction. */
	void BuildReimportUpdatePlan(
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings,
		const FUMGWidgetData& Data,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode* ScopeRoot,
		FUMGWidgetNode& PreparedScopeRoot,
		FDesign2UMGComponentMappingBuildState& MappingBuildState,
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById,
		TArray<FReimportPlanEntry>& Plan,
		TSet<FString>& AliveInstanceWidgetNames)
	{
		// 6) Component Mapping: two-tier match on the IR copy before generation (hit Keys go into the copy; the source data stays untouched).
		PreparedScopeRoot = *ScopeRoot;
		Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
			PreparedScopeRoot, GenerationSettings.ComponentMapping, Data, MappingBuildState);

		// 7) Update plan: manifest instances <-> new IR nodes paired by designNodeId.
		TMap<FString, bool> MappedKindCache;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			InstancesById.Add(Record.DesignNodeId, &Record);

			FReimportPlanEntry Entry;
			Entry.ManifestRecord = &Record;
			Entry.NewNode = FindNodeByDesignId(PreparedScopeRoot, Record.DesignNodeId);
			Entry.bNewKindMapped = Entry.NewNode
				? IsMappedKind(*Entry.NewNode, GenerationSettings.ComponentMapping, MappedKindCache)
				: false;

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

		// Instance widget names still alive in this round's update plan (Matched/KindChanged = still present in the design-side new source).
		// Used as the second conservative gate before the 9a/9b subtree whole delete, preventing live instances from being deleted along the way (see the function comment).
		for (const FReimportPlanEntry& Entry : Plan)
		{
			if (Entry.Action != EReimportPlanAction::Removed)
			{
				AliveInstanceWidgetNames.Add(Entry.ManifestRecord->WidgetName);
			}
		}
	}

	/** Step 8): structural sanity check; failure exits early as a full conflict. */
	TOptional<FDesignReimportResult> CheckReimportStructureViability(
		const FReimportSession& Session,
		UWidgetBlueprint* WBP,
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		const FUMGWidgetNode& PreparedScopeRoot,
		FDesignReimportResult& Result,
		FIntPoint& DesignSize)
	{
		// 8) Structural sanity: target tree missing / not even one manifest instance locatable = project-side restructure, full conflict.
		if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
		{
			return HandleFullConflict(
				TEXT("target widget tree is missing a root widget"),
				Session, MoveTemp(Result.Diagnostics));
		}
		int32 LocatedInstances = 0;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			LocatedInstances += WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)) ? 1 : 0;
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
		if (DesignSize.X <= 1 || DesignSize.Y <= 1)
		{
			return HandleFullConflict(
				TEXT("new source did not provide a valid root design size"),
				Session, MoveTemp(Result.Diagnostics));
		}
		return {};
	}

	/** Step 9) transaction prelude: open the editor transaction + add RF_Transactional to the existing tree; returns the transaction index (INDEX_NONE = no transaction). */
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

	/** Step 10): assemble the post-reimport manifest payload (including preview-size refresh and GUID backfill). */
	FDesign2UMGGenerationOutput BuildReimportManifestPayload(
		const FDesign2UMGGenerationManifestSidecar& Manifest,
		bool bPrototypeHostContinued,
		const FUMGWidgetData& Data,
		FReimportApplyContext& Ctx)
	{
		// 10) Assemble the post-reimport manifest payload.
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
		GenerationOutput.Manifest.MappedInstances = MoveTemp(*Ctx.UpdatedMappedRecords);
		GenerationOutput.Manifest.TopLevelGenerated = MoveTemp(*Ctx.UpdatedGeneratedRecords);

		// Instances kept as-is (conflicts/location failures): backfill their original records into the new manifest.
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Manifest.Instances)
		{
			if (!Ctx.KeepAsIsWidgetNames->Contains(Record.WidgetName))
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

		// Instances newly created by this round's generation chain (top-level added / kind-changed new forms / nested new mapped) enter the new manifest.
		for (const FDesign2UMGMappedInstanceRecord& NewMapped : Ctx.MappingBuildState->GetMappedInstances())
		{
			if (!GenerationOutput.Manifest.MappedInstances.ContainsByPredicate(
				[&NewMapped](const FDesign2UMGMappedInstanceRecord& M) { return M.WidgetName == NewMapped.WidgetName; }))
			{
				GenerationOutput.Manifest.MappedInstances.Add(NewMapped);
			}
		}
		for (const FDesign2UMGGeneratedInstanceRecord& NewGenerated : Ctx.MappingBuildState->GetTopLevelGenerated())
		{
			if (!GenerationOutput.Manifest.TopLevelGenerated.ContainsByPredicate(
				[&NewGenerated](const FDesign2UMGGeneratedInstanceRecord& G) { return G.WidgetName == NewGenerated.WidgetName; }))
			{
				GenerationOutput.Manifest.TopLevelGenerated.Add(NewGenerated);
			}
		}

		// New d2uOwned = (old set - deleted names) U names created this round; then drop names no longer present in the tree.
		for (const FString& CreatedName : Ctx.MappingBuildState->GetCreatedWidgetNames())
		{
			(*Ctx.UpdatedD2UOwned).Add(CreatedName);
		}
		if (Ctx.WBP->WidgetTree)
		{
			for (const FString& OwnedName : (*Ctx.UpdatedD2UOwned).Array())
			{
				if (!Ctx.WBP->WidgetTree->FindWidget(FName(*OwnedName)))
				{
					(*Ctx.UpdatedD2UOwned).Remove(OwnedName);
				}
			}
		}
		GenerationOutput.Manifest.D2UOwnedNames = (*Ctx.UpdatedD2UOwned).Array();
		GenerationOutput.Manifest.D2UOwnedNames.Sort();

		return GenerationOutput;
	}

	/**
	 * Asset finalization at the tail of step 10) (asset-side side effects unrelated to manifest assembly):
	 * refresh the CDO preview size when the design root size changed (written back by SaveWidgetBlueprint after compiling);
	 * backfill missing variable GUIDs of generation-chain-created widgets with the compiler's own deterministic rule.
	 */
	void FinalizeReimportedAssetState(FReimportApplyContext& Ctx)
	{
		if (UUserWidget* CDO = Cast<UUserWidget>(
			Ctx.WBP->GeneratedClass ? Ctx.WBP->GeneratedClass->GetDefaultObject() : nullptr))
		{
			// The CDO lacks RF_Transactional by default (the WidgetTree got it above; same reasoning here):
			// without the flag, Modify never enters the transaction buffer, and a save-failure rollback / user undo would leave the preview size at the new value.
			CDO->SetFlags(RF_Transactional);
			CDO->Modify();
			CDO->DesignTimeSize = FVector2D(Ctx.DesignSize.X, Ctx.DesignSize.Y);
			CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		}

		// Widgets newly created by this round's generation chain (plus test injections via ConstructWidget) have no
		// variable GUID, and the compiler ensures on missing entries. Backfill with the compiler's own deterministic
		// rule; existing GUIDs stay untouched. Modify first to capture the GUID map's pre-transaction state: on a
		// save-failure rollback the map restores with the asset, or the post-rollback recompile would trip the compiler's ensure on a "deleted widget's leftover GUID".
		if (Ctx.WBP->WidgetTree)
		{
			Ctx.WBP->Modify();
			Ctx.WBP->WidgetTree->ForEachWidget([&Ctx](UWidget* TreeWidget)
			{
				const FName WidgetName = TreeWidget->GetFName();
				if (!Ctx.WBP->WidgetVariableNameToGuidMap.Contains(WidgetName))
				{
					Ctx.WBP->WidgetVariableNameToGuidMap.Add(
						WidgetName, FGuid::NewDeterministicGuid(TreeWidget->GetPathName()));
				}
			});
		}
	}

	/** Step 11): writability pre-check + compile & save + failure rollback (FailSaveWithRollback moved with this block). */
	FDesignReimportResult SaveReimportedAsset(
		const FReimportSession& Session,
		const FString& TargetPackage,
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput& GenerationOutput,
		int32 TransactionIndex,
		FDesignReimportResult& Result)
	{
		// 11) Pre-save writability pre-check + compile + save + write the new manifest.
		// Unified save-failure exit: transaction rollback + one recompile pulling the generated class back in line with the template tree + a reimport_save_failed diagnostic.
		const auto FailSaveWithRollback = [&](const FString& Reason)
		{
			// The rollback state is reported as the transaction ownership really is: with no own transaction (transacting
			// unavailable or nested inside an active outer transaction), no End/Undo happens; the disk is untouched and the in-memory changes stay in place.
			FString RollbackNote;
			if (TransactionIndex != INDEX_NONE)
			{
				GEditor->EndTransaction();
				GEditor->UndoTransaction();
				RollbackNote = TEXT("; transaction rolled back, asset keeps its pre-reimport state, manifest not updated");
			}
			else
			{
				RollbackNote = TEXT("; save not completed and no reimport-owned editor transaction was open (transacting unavailable or nested inside an active transaction); asset on disk keeps its previous state, in-memory changes remain, manifest not updated");
			}
			AddReimportDiagnostic(Result.Diagnostics, TEXT("reimport_save_failed"), Reason + RollbackNote);
			// The rollback restores only the template tree; class objects produced by compiling do not roll back with it -
			// one recompile pulls the generated class back into the state of the (restored) template tree.
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			FKismetEditorUtilities::CompileBlueprint(
				WBP,
				EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipFiBSearchMetaUpdate);
			Result.bSucceeded = false;
			// Reimport finished: per-node diagnostics land in the structured report (the same exit as import; see design.md sections 0/6).
			DesignImportDiagnosticReport::WriteReimport(
			Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
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
			if (!IsFileWritableForSave(
				FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension()),
				/*bAllowWriteProbe=*/false, Reason))
			{
				WritabilityBlockers = Reason;
			}
			if (!IsFileWritableForSave(
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

		// 11b) Compile + save + write the new manifest; failure = transaction rollback, asset keeps its original state, manifest not updated.
		if (!FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput))
		{
			FailSaveWithRollback(FString::Printf(
				TEXT("Save/compile failed for %s"),
				*TargetPackage));
			return Result;
		}

		// 11c) Sidecar write-failure propagation: uasset on disk but manifest not updated = asset and record out of
		//      sync, which must not be reported as success (IO failures the pre-check cannot catch, like a full disk, land
		//      here). The disk already holds the new asset: memory is not rolled back (that would desync memory and disk); the transaction concludes normally.
		const bool bManifestWriteFailed = GenerationOutput.MappingDiagnostics.ContainsByPredicate(
			[](const FDesign2UMGMappingDiagnostic& Diagnostic)
			{
				return Diagnostic.Kind == Design2UMGManifestKinds::ManifestWriteFailedDiagnostic;
			});
		if (bManifestWriteFailed)
		{
			for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
			{
				if (Diagnostic.Kind == Design2UMGManifestKinds::ManifestWriteFailedDiagnostic)
				{
					Result.Diagnostics.Add(Diagnostic);
				}
			}
			if (TransactionIndex != INDEX_NONE)
			{
				GEditor->EndTransaction();
			}
			AddReimportDiagnostic(
				Result.Diagnostics, TEXT("reimport_save_failed"),
				FString::Printf(
					TEXT("Asset %s was saved but its generation manifest could not be written; the asset and the manifest may be inconsistent"),
					*TargetPackage));
			Result.bSucceeded = false;
			DesignImportDiagnosticReport::WriteReimport(
			Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
			return Result;
		}

		if (TransactionIndex != INDEX_NONE)
		{
			GEditor->EndTransaction();
		}

		Result.UpdatedBlueprint = WBP;
		Result.bSucceeded = true;
		AddReimportDiagnostic(
			Result.Diagnostics, TEXT("reimport_completed"),
			FString::Printf(
				TEXT("Reimport of %s applied (manifest updated)"),
				*TargetPackage));
		// Reimport finished: per-node diagnostics land in the structured report (the same exit as import; see design.md sections 0/6).
		DesignImportDiagnosticReport::WriteReimport(
			Session.Document, Session.Data, TargetPackage, Result.Diagnostics, &Result.ComponentMappingDiagnostics);
		return Result;
	}
}
