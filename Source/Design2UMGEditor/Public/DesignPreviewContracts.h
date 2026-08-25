// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

class UUserWidget;

/** Output mode requested when materializing a preview snapshot. */
enum class EDesignBlueprintGenerationMode : uint8
{
	BlueprintOnly,
	BlueprintAndPng,
	BlueprintAndCompiledPng
};

/** Blueprint/PNG generation options for preview-approved snapshots. */
struct FBlueprintGenerationOptions
{
	EDesignBlueprintGenerationMode Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	FString CompiledPngFilePath;
};

// Preview materialization only needs the generation-domain subset that can
// affect in-editor widget construction, not the full import settings payload.
struct FDesign2UMGGenerationSettingsSnapshot;

struct FDesignPreviewMaterializationSettingsSnapshot
{
	TArray<FWidgetClassOverride> ClassOverrides;

	// Single copy boundary: new generation fields that preview must observe are
	// registered here once, not at every call site.
	static FDesignPreviewMaterializationSettingsSnapshot FromGenerationSettings(
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
};

struct FPreviewRefreshInput
{
	uint64 RequestId = 0;
	FString EditedJson;
	bool bResolveImages = true;
};

struct FPreviewApprovedSnapshot
{
	uint64 RequestId = 0;
	FString EditedJson;
	FUMGWidgetData Data;
	FString ImageReport;
	TOptional<FDesignPreviewMaterializationSettingsSnapshot> MaterializationSettingsSnapshot;
};

struct FPreviewRefreshResult
{
	bool bSucceeded = false;
	uint64 RequestId = 0;
	FString EditedJson;
	FUMGWidgetData Data;
	FString ImageReport;
	FString ParseError;
};

struct FArtifactGenerationRequest
{
	uint64 SourceRequestId = 0;
	FString OutputPath;
	FBlueprintGenerationOptions Generation;
	bool bResolveImages = false;
	TOptional<FDesignDocument> DiagnosticDocument;
	FString RuntimeImageReport;
	FString DiagnosticImageReportOverride;
};

struct FPreviewGenerationRequest
{
	FPreviewApprovedSnapshot ApprovedSnapshot;
	FArtifactGenerationRequest GenerationRequest;
};

struct FDesignPreviewMaterializationRequest
{
	uint64 RequestId = 0;
	FPreviewApprovedSnapshot ApprovedSnapshot;
};

struct FDesignPreviewMaterializationResult
{
	bool bSucceeded = false;
	uint64 RequestId = 0;
	FVector2D PreviewDesignSize = FVector2D::ZeroVector;
	FString MaterializedWidgetName;
};

class IDesignPreviewRefreshAnalyzer
{
public:
	virtual ~IDesignPreviewRefreshAnalyzer() = default;

	/** Parse edited preview JSON and return generated UMG data plus diagnostics. */
	virtual FPreviewRefreshResult AnalyzePreview(const FPreviewRefreshInput& Input) = 0;
};

/** Service boundary that owns in-editor preview widget materialization. */
class IDesignPreviewMaterializationService
{
public:
	virtual ~IDesignPreviewMaterializationService() = default;

	/** Materialize the approved snapshot into a preview widget. */
	virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) = 0;
	/** Return the currently materialized preview widget, if any. */
	virtual UUserWidget* GetMaterializedPreviewWidget() const = 0;
	/** Release preview state held by the service. */
	virtual void Reset() = 0;
};
