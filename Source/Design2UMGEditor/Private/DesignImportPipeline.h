// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGCommitResult.h"
#include "DesignImportGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGComponentMappingConfig.h"
#include "DesignPreviewContracts.h"
#include "Blueprint/UserWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "DesignImportTypes.h"
#include "UMGSchema.h"

class UWidgetBlueprint;
class UDesign2UMGSettings;
class SWidget;

/**
 * Immutable import/generation settings captured for one import.
 * Internal to the Editor module.
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

/** Metadata produced while analyzing widget data in place. */
struct FImportInPlaceAnalysisResult
{
	FString ErrorMessage;
	FDesignImageImportResult ImageResult;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
};

/** Document analysis owns the converted widget tree it produces. */
struct FImportAnalysisResult : FImportInPlaceAnalysisResult
{
	FUMGWidgetData Data;
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
	TOptional<FDesignImageImportResult> DiagnosticImageResult;
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
	/** Actual compiled widget instance used for the render, retained for immediate caller-side inspection. */
	TStrongObjectPtr<UUserWidget> RenderedWidget;
	/** Keeps that render's Slate tree alive so its cached geometry remains inspectable. */
	TSharedPtr<SWidget> RenderedSlateWidget;
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
	FDesignImageImportResult ImageResult;
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
		const FDesignImageImportResult& ImageResult = FDesignImageImportResult(),
		const FString& RequestedOutputPath = FString(),
		const FString& RequestedRenderPngPath = FString(),
		const FString& GeneratedAssetPath = FString(),
		const FString& RenderPngPath = FString());
	bool LoadPenDocument(const FString& FilePath, const FString& TargetNodeId, FDesignDocument& OutDocument, FString* OutError = nullptr);
	/**
	 * Resolve existing relative image files against the .pen source directory.
	 * Missing files, remote URLs, asset paths and absolute paths remain unchanged.
	 * MCP documents without a local source directory skip this step.
	 */
	void AnchorRelativeImageFillUrls(FDesignDocument& Document, const FString& PenFilePath);
	FDesignImportExecutionResult ExecutePenFile(const FString& FilePath, const FString& TargetNodeId, const FDesignImportRequest& Request);
	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames);
}
