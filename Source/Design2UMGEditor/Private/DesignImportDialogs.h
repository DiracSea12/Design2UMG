// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SDesign2UMGPreviewWindow.h"

struct FDesignFrameInfo;
struct FUMGWidgetData;
struct FDesignDocument;
struct FImportAnalysisInput;
struct FImportAnalysisResult;

namespace DesignImportDialogs
{
	/** Interactive preparation retains the source on failure; the business analyzer never installs tools. */
	FImportAnalysisResult AnalyzeForPreview(FDesignDocument& Document, const FImportAnalysisInput& Input);
	FString FormatImportResult(const FUMGWidgetData& Data, const FString& ErrorMessage, bool bSucceeded,
		const FString& SourceFilePath = FString());
	FString FormatImageIssues(const FDesignImageImportResult& Result);
	void ShowImportProblem(const FString& Message);

	using FFrameSelectedCallback = TFunction<void(const FString& FrameId)>;

	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& SourceFilePath,
		const FText& WindowTitle,
		FOnPreviewConfirmed OnConfirmed,
		FOnPreviewCancelled OnCancelled = FOnPreviewCancelled(),
		const TOptional<FDesignPreviewMaterializationSettingsSnapshot>& InitialMaterializationSettingsSnapshot =
			TOptional<FDesignPreviewMaterializationSettingsSnapshot>(),
		const FDesignImageImportResult& InitialImageResult = FDesignImageImportResult());

	void ShowFrameSelector(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected);
}
