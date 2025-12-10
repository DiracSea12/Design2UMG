// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"
#include "Blueprint/UserWidget.h"

DECLARE_DELEGATE_FiveParams(FOnPreviewConfirmed, const FString& /*EditedJson*/, const FString& /*OutputPath*/, const TArray<uint8>& /*SourceImageData*/, FIntPoint /*SourceSize*/, FVector2D /*CoordScale*/);
DECLARE_DELEGATE(FOnPreviewCancelled);
DECLARE_DELEGATE_FourParams(FOnPreviewRequestAI, const TArray<uint8>& /*ImageData*/, FIntPoint /*ImageSize*/, TFunction<void(const FString&)> /*OnSuccess*/, TFunction<void(const FString&)> /*OnError*/);

/**
 * ImageToUMG 预览窗口 - 显示 AI 生成的 JSON 并允许编辑
 */
class SImageToUMGPreviewWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImageToUMGPreviewWindow) {}
		SLATE_ARGUMENT(FString, OriginalJson)
		SLATE_ARGUMENT(TWeakObjectPtr<UTexture2D>, SourceTexture)
		SLATE_ARGUMENT(FString, OutputPath)
		SLATE_ARGUMENT(TArray<uint8>, SourceImageData)
		SLATE_ARGUMENT(FIntPoint, SourceImageSize)
		SLATE_ARGUMENT(FIntPoint, AIImageSize)
		SLATE_EVENT(FOnPreviewConfirmed, OnConfirmed)
		SLATE_EVENT(FOnPreviewCancelled, OnCancelled)
		SLATE_EVENT(FOnPreviewRequestAI, OnRequestAIAnalysis)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SImageToUMGPreviewWindow();

private:
	TSharedPtr<SMultiLineEditableTextBox> JsonEditor;
	TSharedPtr<SEditableTextBox> OutputPathBox;
	TSharedPtr<FSlateBrush> ImageBrush;
	TSharedPtr<SBox> UMGPreviewContainer;
	TStrongObjectPtr<UUserWidget> PreviewWidget;  // 使用强引用防止GC
	TArray<TStrongObjectPtr<UTexture2D>> PreviewTextures;  // 预览用临时纹理

	TArray<uint8> SourceImageData;
	FIntPoint SourceImageSize;
	FIntPoint AIImageSize;
	FVector2D CoordScale = FVector2D(1.0f, 1.0f);  // 分别存储X/Y缩放因子
	FString CurrentJson;
	FString CurrentOutputPath;
	bool bAIAnalysisInProgress = false;

	FOnPreviewConfirmed OnConfirmed;
	FOnPreviewCancelled OnCancelled;
	FOnPreviewRequestAI OnRequestAIAnalysis;

	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnFormatJsonClicked();
	FReply OnPreviewUMGClicked();
	FReply OnSendToAIClicked();

	void RefreshUMGPreview();
	void CleanupPreviewWidget();
	void OnJsonTextChanged(const FText& NewText);
	void OnOutputPathChanged(const FText& NewText);

	bool ValidateJson(const FString& JsonString, FString& OutError);
	FString FormatJson(const FString& JsonString);
	UUserWidget* CreatePreviewWidgetFromJson(const FString& JsonString);

	// UI 构建辅助函数
	TSharedRef<SWidget> BuildSourceImagePanel();
	TSharedRef<SWidget> BuildUMGPreviewPanel();
	TSharedRef<SWidget> BuildJsonEditorPanel();
	TSharedRef<SWidget> BuildBottomBar();

	// 防抖自动刷新
	void SchedulePreviewRefresh();
	FTimerHandle PreviewRefreshTimerHandle;
};
