// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Blueprint/UserWidget.h"

struct FUMGWidgetData;

DECLARE_DELEGATE_TwoParams(FOnPreviewConfirmed, const FString& /*EditedJson*/, const FString& /*OutputPath*/);
DECLARE_DELEGATE(FOnPreviewCancelled);

/** 控件调整数据 - 用于UI显示和编辑 */
struct FWidgetAdjustData
{
	FString Name;
	FString Type;
	FString JsonPath;  // 在JSON中的路径，用于定位和更新
	float PosX = 0;    // Offsets.Left
	float PosY = 0;    // Offsets.Top
	float Width = 0;   // Offsets.Right (点锚点=宽度, 拉伸锚点=右边距)
	float Height = 0;  // Offsets.Bottom (点锚点=高度, 拉伸锚点=下边距)
	bool bStretchX = false;  // X轴拉伸锚点 (Min.X != Max.X)
	bool bStretchY = false;  // Y轴拉伸锚点 (Min.Y != Max.Y)
	int32 Depth = 0;   // 嵌套深度，用于缩进显示
};

/**
 * PenToUMG 预览窗口 - 显示生成的 JSON 并允许编辑
 */
class SPenToUMGPreviewWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPenToUMGPreviewWindow) {}
		SLATE_ARGUMENT(FString, OriginalJson)
		SLATE_ARGUMENT(FString, OutputPath)
		SLATE_EVENT(FOnPreviewConfirmed, OnConfirmed)
		SLATE_EVENT(FOnPreviewCancelled, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPenToUMGPreviewWindow();

private:
	TSharedPtr<SMultiLineEditableTextBox> JsonEditor;
	TSharedPtr<SEditableTextBox> OutputPathBox;
	TSharedPtr<SBox> UMGPreviewContainer;
	TSharedPtr<SVerticalBox> WidgetAdjustList;  // 控件调整列表容器
	TStrongObjectPtr<UUserWidget> PreviewWidget;

	float GlobalScaleFactor = 1.0f;
	FString CurrentJson;
	FString CurrentOutputPath;

	// 控件调整数据
	TArray<FWidgetAdjustData> WidgetAdjustItems;
	bool bUpdatingFromAdjustPanel = false;  // 防止双向同步时的无限循环

	FOnPreviewConfirmed OnConfirmed;
	FOnPreviewCancelled OnCancelled;

	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnFormatJsonClicked();
	FReply OnPreviewUMGClicked();

	void RefreshUMGPreview();
	void DoRefreshPreview(const FString& JsonString);
	void CleanupPreviewWidget();
	void OnJsonTextChanged(const FText& NewText);
	void OnOutputPathChanged(const FText& NewText);

	bool ValidateJson(const FString& JsonString, FString& OutError);
	FString FormatJson(const FString& JsonString);
	UUserWidget* CreatePreviewWidgetFromJson(const FUMGWidgetData& WidgetData);

	// UI 构建辅助函数
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
