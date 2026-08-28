// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesign2UMGMappingDiagnostic;
struct FUMGWidgetData;

namespace DesignImportDiagnosticReport
{
	/** Artifact and path fields of the import/reimport diagnostic report (Write's structured payload, replacing 5 positional strings). */
	struct FImportReportArtifacts
	{
		/** Image parsing report text (multi-line; source of image failure entries); optional. */
		FString ImageReport;
		/** Request-side output location: import = requested output directory, reimport = target asset package path. */
		FString RequestedOutputPath;
		FString RequestedRenderPngPath;
		FString GeneratedAssetPath;
		FString RenderPngPath;
	};

	void Write(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FImportReportArtifacts& Artifacts,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics = nullptr,
		const TArray<FDesign2UMGMappingDiagnostic>* ReimportDiagnostics = nullptr);

	/**
	 * Report exit for Safe Reimport: shares the import report format and file location
	 * (ImportReports/*.import_report.json). The target asset goes to requestedOutputPath;
	 * per-node diagnostics go into the top-level reimport section (kind/message entries, same shape as componentMapping).
	 * ComponentMappingDiagnostics (optional) lands in the componentMapping section, the same classification exit as the import path.
	 */
	void WriteReimport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& TargetAssetPath,
		const TArray<FDesign2UMGMappingDiagnostic>& ReimportDiagnostics,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics = nullptr);
}
