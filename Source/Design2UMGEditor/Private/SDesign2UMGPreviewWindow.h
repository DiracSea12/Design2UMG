// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPreviewContracts.h"
#include "TimerManager.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"

class SBox;
struct FArtifactGenerationResult;

DECLARE_DELEGATE_RetVal_OneParam(FArtifactGenerationResult, FOnPreviewConfirmed, const FPreviewGenerationRequest& /*GenerationRequest*/);
DECLARE_DELEGATE(FOnPreviewCancelled);

/**
 * Editor preview UI contract for Design2UMG preview workflows.
 */
class DESIGN2UMGEDITOR_API SDesign2UMGPreviewWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDesign2UMGPreviewWindow) {}
		SLATE_ARGUMENT(FString, OriginalJson)
		SLATE_ARGUMENT(TSharedPtr<const FUMGWidgetData>, OriginalData)
		SLATE_ARGUMENT(FDesignImageImportResult, InitialImageResult)
		SLATE_ARGUMENT(FString, OutputPath)
		SLATE_ARGUMENT(TArray<FString>, Warnings)
		SLATE_ARGUMENT(TOptional<FDesignPreviewMaterializationSettingsSnapshot>, InitialMaterializationSettingsSnapshot)
		SLATE_ARGUMENT(TSharedPtr<IDesignPreviewRefreshAnalyzer>, PreviewRefreshAnalyzer)
		SLATE_ARGUMENT(TSharedPtr<IDesignPreviewMaterializationService>, PreviewMaterializationService)
		SLATE_EVENT(FOnPreviewConfirmed, OnConfirmed)
		SLATE_EVENT(FOnPreviewCancelled, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDesign2UMGPreviewWindow();

private:
	friend struct FDesign2UMGPreviewWindowTestAccess;

	TSharedPtr<SMultiLineEditableTextBox> JsonEditor;
	TSharedPtr<SEditableTextBox> OutputPathBox;
	TSharedPtr<SBox> UMGPreviewContainer;
	TSharedPtr<SBox> PreviewDesignFrame;
	bool bFitPreviewToWindow = true;
	TSharedPtr<IDesignPreviewRefreshAnalyzer> PreviewRefreshAnalyzer;
	TSharedPtr<IDesignPreviewMaterializationService> PreviewMaterializationService;

	FString CurrentJson;
	TSharedPtr<const FUMGWidgetData> OriginalData;
	FDesignImageImportResult InitialImageResult;
	TOptional<FDesignImageImportResult> CurrentImageResult;
	FText GenerationStatus;
	FString CurrentOutputPath;
	TArray<FString> ConversionWarnings;
	uint64 NextRefreshRequestId = 1;
	uint64 LatestRequestedRefreshId = 0;
	TOptional<FPreviewApprovedSnapshot> LatestApprovedSnapshot;
	TOptional<FDesignPreviewMaterializationSettingsSnapshot> CachedMaterializationSettingsSnapshot;
	bool bLatestApprovedSnapshotStale = true;

	FOnPreviewConfirmed OnConfirmed;
	FOnPreviewCancelled OnCancelled;

	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnFormatJsonClicked();

	void RefreshUMGPreview();
	void DoRefreshPreview(uint64 RequestId, const FString& JsonString);
	void CleanupPreviewWidget();
	void UpdatePreviewViewport();
	TOptional<FDesignPreviewMaterializationSettingsSnapshot> ResolveMaterializationSettingsSnapshot();
	void OnJsonTextChanged(const FText& NewText);
	void OnOutputPathChanged(const FText& NewText);
	void InvalidateApprovedSnapshot();
	bool CanConfirmLatestSnapshot() const
	{
		return !bLatestApprovedSnapshotStale
			&& LatestApprovedSnapshot.IsSet()
			&& LatestApprovedSnapshot->RequestId == LatestRequestedRefreshId;
	}

	bool ValidateJson(const FString& JsonString, FString& OutError);
	FString FormatJson(const FString& JsonString);

	TSharedRef<SWidget> BuildUMGPreviewPanel();
	TSharedRef<SWidget> BuildJsonEditorPanel();
	TSharedRef<SWidget> BuildBottomBar();

	void SchedulePreviewRefresh();
	FTimerHandle PreviewRefreshTimerHandle;
};

