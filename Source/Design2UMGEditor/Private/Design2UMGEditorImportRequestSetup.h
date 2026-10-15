// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"

class FJsonObject;

namespace Design2UMGEditorImportRequestSetup
{
	FDesign2UMGImportSettingsSnapshot CaptureSettingsSnapshot();

	FDesignImportRequest MakeImportRequest(
		const FString& OutputPath,
		const FText& PreviewWindowTitle,
		bool bUsePreview,
		EDesignBlueprintGenerationMode GenerationMode,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		bool bResolveImages = true,
		const TOptional<FDesignDocument>& DiagnosticDocument = TOptional<FDesignDocument>());

	FDesignImportRequest MakePenImportRequest(
		const FString& OutputPath,
		bool bUsePreview,
		bool bRenderPng,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot);

	FDesignImportExecutionResult ExecuteImportWithSettings(
		FUMGWidgetData& Data,
		const FDesignImportRequest& Request);

	FDesignImportExecutionResult ExecuteImportWithSettings(
		FDesignDocument& Document,
		const FDesignImportRequest& Request,
		const FImportAnalysisResult* PrecomputedAnalysis = nullptr);

	FDesignImportExecutionResult ExecutePenFileImportWithSettings(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request);

	TSharedPtr<FJsonObject> MakePencilReadDocumentArgs();
}
