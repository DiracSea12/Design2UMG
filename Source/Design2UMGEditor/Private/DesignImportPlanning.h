// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"

namespace DesignImportPlanning
{
	void ResolveImages(FUMGWidgetData& Data, FString* OutReport = nullptr);

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
