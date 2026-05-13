// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

class UUserWidget;

enum class EDesignBlueprintGenerationMode : uint8
{
	BlueprintOnly,
	BlueprintAndPng,
	BlueprintAndCompiledPng
};

struct FBlueprintGenerationOptions
{
	EDesignBlueprintGenerationMode Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	FString CompiledPngFilePath;
};

// Preview materialization only needs the generation-domain subset that can
// affect in-editor widget construction, not the full import settings payload.
struct FDesignPreviewMaterializationSettingsSnapshot
{
	TArray<FWidgetClassOverride> ClassOverrides;
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

	virtual FPreviewRefreshResult AnalyzePreview(const FPreviewRefreshInput& Input) = 0;
};

class IDesignPreviewMaterializationService
{
public:
	virtual ~IDesignPreviewMaterializationService() = default;

	virtual bool MaterializePreview(const FDesignPreviewMaterializationRequest& Request, FDesignPreviewMaterializationResult& OutResult) = 0;
	virtual UUserWidget* GetMaterializedPreviewWidget() const = 0;
	virtual void Reset() = 0;
};
