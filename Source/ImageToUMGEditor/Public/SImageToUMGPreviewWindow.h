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

/** 控件调整数据 - 用于UI显示和编辑 */
struct FWidgetAdjustData
{
	FString Name;
	FString Type;
	FString JsonPath;  // 在JSON中的路径，用于定位和更新
	float PosX = 0;    // Offsets.Left
	float PosY = 0;    // Offsets.Top
	float Width = 0;   // Offsets.Right (点锚点时为宽度)
	float Height = 0;  // Offsets.Bottom (点锚点时为高度)
	int32 Depth = 0;   // 嵌套深度，用于缩进显示
};

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
	TSharedPtr<SVerticalBox> WidgetAdjustList;  // 控件调整列表容器
	TStrongObjectPtr<UUserWidget> PreviewWidget;
	TArray<TStrongObjectPtr<UTexture2D>> PreviewTextures;

	TArray<uint8> SourceImageData;
	FIntPoint SourceImageSize;
	FIntPoint AIImageSize;
	FVector2D CoordScale = FVector2D(1.0f, 1.0f);
	float GlobalScaleFactor = 1.0f;
	FString CurrentJson;
	FString CurrentOutputPath;
	bool bAIAnalysisInProgress = false;

	// 控件调整数据
	TArray<FWidgetAdjustData> WidgetAdjustItems;
	bool bUpdatingFromAdjustPanel = false;  // 防止双向同步时的无限循环

	FOnPreviewConfirmed OnConfirmed;
	FOnPreviewCancelled OnCancelled;
	FOnPreviewRequestAI OnRequestAIAnalysis;

	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnFormatJsonClicked();
	FReply OnPreviewUMGClicked();
	FReply OnSendToAIClicked();

	void RefreshUMGPreview();
	void DoRefreshPreview(const FString& JsonString);
	void CleanupPreviewWidget();
	void OnJsonTextChanged(const FText& NewText);
	void OnOutputPathChanged(const FText& NewText);

	bool ValidateJson(const FString& JsonString, FString& OutError);
	FString FormatJson(const FString& JsonString);
	UUserWidget* CreatePreviewWidgetFromJson(const FString& JsonString);

	// UI 构建辅助函数
	TSharedRef<SWidget> BuildSourceImagePanel();
	TSharedRef<SWidget> BuildUMGPreviewPanel();
	TSharedRef<SWidget> BuildRightPanel();       // 右侧面板（JSON编辑+控件调整）
	TSharedRef<SWidget> BuildJsonEditorPanel();
	TSharedRef<SWidget> BuildWidgetAdjustPanel();
	TSharedRef<SWidget> BuildBottomBar();

	// 防抖自动刷新
	void SchedulePreviewRefresh();
	FTimerHandle PreviewRefreshTimerHandle;

	// 全局缩放因子控制
	float GetGlobalScaleFactor() const;
	void OnGlobalScaleFactorChanged(float NewValue);
	FText GetGlobalScaleFactorText() const;

	// 控件调整相关
	void ParseWidgetsFromJson();  // 从JSON解析控件列表
	void RebuildWidgetAdjustList();  // 重建调整UI
	void OnWidgetPropertyChanged(int32 WidgetIndex, const FString& PropertyName, float NewValue);
	void UpdateJsonFromAdjustData();  // 将调整数据写回JSON
};
