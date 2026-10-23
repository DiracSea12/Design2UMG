// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"

class FJsonObject;

namespace Design2UMGEditorImportRequestSetup
{
	FDesign2UMGImportSettingsSnapshot CaptureSettingsSnapshot();

	/** Read the active in-memory document through Pencil execute. */
	TSharedPtr<FJsonObject> MakePencilReadDocumentArgs();

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
}
