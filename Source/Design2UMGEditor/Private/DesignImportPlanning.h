// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"

namespace DesignImportPlanning
{
	/** Mark download-stage warnings so a successful download retry can retract them. */
	FString MakeResourceResolutionWarning(const FString& Message);

	void ResolveImages(FUMGWidgetData& Data, FString* OutReport = nullptr);

	/** Preserve unresolved Figma refs until the source is resolved and converted again. */
	void RecordImageResolutionFailure(FUMGWidgetData& Data);

	FImportAnalysisResult AnalyzeWidgetData(
		FUMGWidgetData& Data,
		const FImportAnalysisInput& Input);

	FImportAnalysisResult AnalyzeDocument(
		FDesignDocument& Document,
		const FImportAnalysisInput& Input);

	FPreviewRefreshResult AnalyzePreviewRefreshInput(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges = nullptr);
}
