// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Blueprint/UserWidget.h"

struct FUMGWidgetData;

DECLARE_DELEGATE_TwoParams(FOnPreviewConfirmed, const FString& /*EditedJson*/, const FString& /*OutputPath*/);
DECLARE_DELEGATE(FOnPreviewCancelled);

class SDesign2UMGPreviewWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDesign2UMGPreviewWindow) {}
		SLATE_ARGUMENT(FString, OriginalJson)
		SLATE_ARGUMENT(FString, OutputPath)
		SLATE_ARGUMENT(TArray<FString>, Warnings)
		SLATE_EVENT(FOnPreviewConfirmed, OnConfirmed)
		SLATE_EVENT(FOnPreviewCancelled, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDesign2UMGPreviewWindow();

private:
	TSharedPtr<SMultiLineEditableTextBox> JsonEditor;
	TSharedPtr<SEditableTextBox> OutputPathBox;
	TSharedPtr<SBox> UMGPreviewContainer;
	TStrongObjectPtr<UUserWidget> PreviewWidget;

	FString CurrentJson;
	FString CurrentOutputPath;
	TArray<FString> ConversionWarnings;

	FOnPreviewConfirmed OnConfirmed;
	FOnPreviewCancelled OnCancelled;

	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnFormatJsonClicked();

	void RefreshUMGPreview();
	void DoRefreshPreview(const FString& JsonString);
	void CleanupPreviewWidget();
	void OnJsonTextChanged(const FText& NewText);
	void OnOutputPathChanged(const FText& NewText);

	bool ValidateJson(const FString& JsonString, FString& OutError);
	FString FormatJson(const FString& JsonString);
	UUserWidget* CreatePreviewWidgetFromJson(const FUMGWidgetData& WidgetData);

	TSharedRef<SWidget> BuildUMGPreviewPanel();
	TSharedRef<SWidget> BuildJsonEditorPanel();
	TSharedRef<SWidget> BuildBottomBar();

	void SchedulePreviewRefresh();
	FTimerHandle PreviewRefreshTimerHandle;
};
