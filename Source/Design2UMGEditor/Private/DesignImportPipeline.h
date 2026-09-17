// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGCommitResult.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGComponentMappingConfig.h"
#include "DesignPreviewContracts.h"
#include "DesignImportTypes.h"
#include "UMGSchema.h"

class UWidgetBlueprint;
class UDesign2UMGSettings;

/**
 * Immutable point-in-time capture of the import/generation settings for one import run.
 * Module-internal pipeline contract (Editor module only; not part of the Public API).
 * Naming convention for this contract family: "Snapshot" types capture immutable
 * settings copies, while "State" types (BuildState, ImportStateSubsystem) track
 * mutable progress during a run.
 */
struct FDesign2UMGImportSettingsSnapshot
{
	FDesign2UMGConversionSettings ConversionSettings;
	FDesign2UMGGenerationSettingsSnapshot GenerationSettings;

	static FDesign2UMGImportSettingsSnapshot FromSettings(const UDesign2UMGSettings* Settings);
};

// Import-core seams: analysis, preview, and artifact generation exchange
// explicit request/result payloads so callers do not depend on UI state.
struct FImportAnalysisInput
{
	bool bResolveImages = true;
	const FDesign2UMGImportSettingsSnapshot* SettingsSnapshot = nullptr;
};

struct FImportAnalysisResult
{
	FUMGWidgetData Data;
	FString ErrorMessage;
	FString ImageReport;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
};

struct FArtifactGenerationResult
{
	FDesign2UMGCommitResult Commit;
	/** True only when commit and requested artifacts succeeded; render can fail after commit. */
	bool bSucceeded = false;
	/** Structured failure reason (e.g. render target size above the RHI texture limit); empty on success or unspecified failures. */
	FString ErrorMessage;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;

	/** Component Mapping diagnostics (ini validation + hit/fallback/inputs/Prototype boundaries), going into the import diagnostic report. */
	TArray<FDesign2UMGMappingDiagnostic> ComponentMappingDiagnostics;
};

struct FDesignImportRequest
{
	FString OutputPath;
	FText PreviewWindowTitle;
	bool bUsePreview = true;
	bool bResolveImages = true;
	FBlueprintGenerationOptions Generation;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
	TOptional<FDesignDocument> DiagnosticDocument;
	FString DiagnosticImageReport;
};

struct FDesignImportExecutionResult
{
	FDesign2UMGCommitResult Commit;
	/** Preview-open success or completed artifacts; preview alone never implies a commit. */
	bool bSucceeded = false;
	bool bPreviewOpened = false;
	UWidgetBlueprint* GeneratedBlueprint = nullptr;
	/** Structured failure reason propagated from artifact generation; empty on success or unspecified failures. */
	FString ErrorMessage;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
};

/** Reimport request: target WBP asset + new source data (the new source is passed via Reimport's Document parameter). */
struct FDesignReimportRequest
{
	/** Package path of the target page-host WBP (/Game/.../WBP_X). */
	FString TargetAssetPath;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
	/** Whether to generate a sibling WBP copy on full conflict; default keeps the original asset + conflict diagnostics, never implicitly creating a copy. */
	bool bGenerateResolutionCopy = false;
};

/** Reimport result: per-node diagnostics go into Diagnostics (kind prefix reimport_*). */
struct FDesignReimportResult
{
	FDesign2UMGCommitResult Commit;
	/** Final Reimport maps to Commit.IsSuccess(); unsaved ApplyPreparedReimport reports apply success only. */
	bool bSucceeded = false;
	/** True when reimport intentionally preserved/skipped some design state while continuing successfully. */
	bool bHasDegradation = false;
	/** Relationship between target and generation records is broken (manifest missing / scope mismatch / structure mismatch); no in-place update happened. */
	bool bFullConflict = false;
	/** Path of the newly generated sibling copy asset when bGenerateResolutionCopy took effect. */
	FString ResolutionCopyPath;
	/** A requested resolution copy has its own commit; the conflicted target remains untouched. */
	TOptional<FDesign2UMGCommitResult> ResolutionCopyCommit;
	UWidgetBlueprint* UpdatedBlueprint = nullptr;
	TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
	/** Component Mapping diagnostics (ini validation + hit/fallback), going into the report's componentMapping section (same channel as the import path). */
	TArray<FDesign2UMGMappingDiagnostic> ComponentMappingDiagnostics;
};

namespace DesignImportPipeline
{
	FImportAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FPreviewRefreshResult AnalyzePreviewRefresh(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges = nullptr);
	FArtifactGenerationResult GenerateArtifacts(FUMGWidgetData& Data, const FArtifactGenerationRequest& Request);
	FArtifactGenerationResult GenerateArtifacts(
		FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request);
	FArtifactGenerationResult GenerateArtifacts(
		const FPreviewApprovedSnapshot& ApprovedSnapshot,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request);
	/** PrecomputedAnalysis lets a caller that already ran AnalyzeImport with this request's settings reuse it instead of a second parse->convert pass. */
	FDesignImportExecutionResult Execute(
		FDesignDocument& Document,
		const FDesignImportRequest& Request,
		const FImportAnalysisResult* PrecomputedAnalysis = nullptr);

	/**
	 * Safe Reimport: load manifest -> re-parse source -> DocumentScope match -> Convert ->
	 * update plan -> apply inside an editor transaction -> compile & save -> manifest/diagnostics.
	 * Only D2U-owned content is updated in place; Commit reports the actual save and recovery outcome;
	 * a broken relationship is treated as a full conflict (original asset kept by default).
	 */
	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request);
	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport = FString(),
		const FString& RequestedOutputPath = FString(),
		const FString& RequestedRenderPngPath = FString(),
		const FString& GeneratedAssetPath = FString(),
		const FString& RenderPngPath = FString());
	bool LoadPenDocument(const FString& FilePath, const FString& TargetNodeId, FDesignDocument& OutDocument, FString* OutError = nullptr);
	/**
	 * Anchors relative image-fill urls of a parsed Pen document to the .pen source directory
	 * (Pencil's own relative-path semantics: sibling files of the .pen document). Only urls whose
	 * anchored file exists are rewritten; broken links keep the original value so downstream skip
	 * behavior matches Pencil opening the same document (missing image). Remote urls, asset paths,
	 * and absolute paths pass through untouched. Shared by every LoadPenDocument entry point that
	 * knows the .pen file location; MCP-sourced documents have no local directory and skip this.
	 */
	void AnchorRelativeImageFillUrls(FDesignDocument& Document, const FString& PenFilePath);
	FDesignImportExecutionResult ExecutePenFile(const FString& FilePath, const FString& TargetNodeId, const FDesignImportRequest& Request);
	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames);
}
