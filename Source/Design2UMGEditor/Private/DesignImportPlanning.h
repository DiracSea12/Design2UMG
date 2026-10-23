// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImageDiagnostics.h"
#include "DesignImportPipeline.h"

namespace DesignImportPlanning
{
	/** Convert document data without resolving images; import execution handles those. */
	FImportAnalysisResult AnalyzeConversion(
		FDesignDocument& Document, const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot);

	FDesignImageImportResult ResolveImages(
		FUMGWidgetData& Data,
		const FDesignImageImportResult* PreviousResult = nullptr,
		const FDesignImageImportSettingsSnapshot* Settings = nullptr);

	/** Preserve unresolved Figma refs until the source is resolved and converted again. */
	void RecordImageResolutionFailure(FDesignImageImportResult& InOutResult);

	FImportInPlaceAnalysisResult AnalyzeImport(
		FUMGWidgetData& Data,
		const FImportAnalysisInput& Input = FImportAnalysisInput());

	FImportAnalysisResult AnalyzeImport(
		FDesignDocument& Document,
		const FImportAnalysisInput& Input = FImportAnalysisInput());

	FPreviewRefreshResult AnalyzePreviewRefreshInput(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges = nullptr);
}
