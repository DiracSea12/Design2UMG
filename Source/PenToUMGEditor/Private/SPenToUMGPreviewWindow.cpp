// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "SPenToUMGPreviewWindow.h"
#include "UMGWidgetUtils.h"
#include "UMGJsonParser.h"
#include "PenRemoteImageImporter.h"
#include "PenToUMGLog.h"
#include "PenToUMGConstants.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "PreviewUserWidget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/ContentWidget.h"

#define LOCTEXT_NAMESPACE "SPenToUMGPreviewWindow"

void SPenToUMGPreviewWindow::Construct(const FArguments& InArgs)
{
	CurrentJson = InArgs._OriginalJson;
	CurrentOutputPath = InArgs._OutputPath;
	ConversionWarnings = InArgs._Warnings;
	OnConfirmed = InArgs._OnConfirmed;
	OnCancelled = InArgs._OnCancelled;

	CurrentJson = FormatJson(CurrentJson);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewTitle", "PenToUMG Preview - Review and edit the generated JSON before creating the Widget Blueprint"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 0, 8, 8)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.6f)[ BuildUMGPreviewPanel() ]
			+ SSplitter::Slot().Value(0.4f)[ BuildRightPanel() ]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			BuildBottomBar()
		]
	];

	// 解析控件列表
	ParseWidgetsFromJson();

	// 自动刷新预览
	RefreshUMGPreview();
}

TSharedRef<SWidget> SPenToUMGPreviewWindow::BuildUMGPreviewPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UMGPreview", "UMG Preview"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(16, 0, 4, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ScaleLabel", "Scale:"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(100)
					[
						SNew(SSlider)
						.MinValue(0.1f)
						.MaxValue(3.0f)
						.Value(this, &SPenToUMGPreviewWindow::GetGlobalScaleFactor)
						.OnValueChanged(this, &SPenToUMGPreviewWindow::OnGlobalScaleFactorChanged)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(40)
					[
						SNew(STextBlock)
						.Text(this, &SPenToUMGPreviewWindow::GetGlobalScaleFactorText)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshPreview", "Refresh Preview"))
					.OnClicked(this, &SPenToUMGPreviewWindow::OnPreviewUMGClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(UMGPreviewContainer, SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
					.Padding(8)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ClickToPreview", "Click 'Refresh Preview' to see the UMG layout"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						.Justification(ETextJustify::Center)
					]
				]
			]
		];
}

TSharedRef<SWidget> SPenToUMGPreviewWindow::BuildRightPanel()
{
	return SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+ SSplitter::Slot().Value(0.5f)[ BuildJsonEditorPanel() ]
		+ SSplitter::Slot().Value(0.5f)[ BuildWidgetAdjustPanel() ];
}

TSharedRef<SWidget> SPenToUMGPreviewWindow::BuildJsonEditorPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("JsonEditor", "JSON Structure (Editable)"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("FormatJson", "Format JSON"))
					.OnClicked(this, &SPenToUMGPreviewWindow::OnFormatJsonClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(JsonEditor, SMultiLineEditableTextBox)
				.Text(FText::FromString(CurrentJson))
				.OnTextChanged(this, &SPenToUMGPreviewWindow::OnJsonTextChanged)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
				.AutoWrapText(false)
				.Margin(4)
			]
		];
}

TSharedRef<SWidget> SPenToUMGPreviewWindow::BuildBottomBar()
{
	TSharedRef<SVerticalBox> BottomBox = SNew(SVerticalBox);

	// 降级警告摘要
	if (ConversionWarnings.Num() > 0)
	{
		FString WarningText = FString::Printf(TEXT("%d element(s) degraded during conversion:"), ConversionWarnings.Num());
		for (const FString& W : ConversionWarnings)
		{
			WarningText += TEXT("\n  - ") + W;
		}

		BottomBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 6)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.25f, 0.18f, 0.0f, 1.0f))
			.Padding(FMargin(8, 4))
			[
				SNew(STextBlock)
				.Text(FText::FromString(WarningText))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.AutoWrapText(true)
			]
		];
	}

	BottomBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputPath", "Output Path:"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(OutputPathBox, SEditableTextBox)
				.Text(FText::FromString(CurrentOutputPath))
				.OnTextChanged(this, &SPenToUMGPreviewWindow::OnOutputPathChanged)
			]
		];

	BottomBox->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Cancel", "Cancel"))
			.OnClicked(this, &SPenToUMGPreviewWindow::OnCancelClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Generate", "Generate Widget Blueprint"))
			.OnClicked(this, &SPenToUMGPreviewWindow::OnGenerateClicked)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		]
	];

	return BottomBox;
}

SPenToUMGPreviewWindow::~SPenToUMGPreviewWindow()
{
	// 清理防抖定时器
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
	}
	CleanupPreviewWidget();
}

FReply SPenToUMGPreviewWindow::OnGenerateClicked()
{
	FString Error;
	if (!ValidateJson(CurrentJson, Error))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("InvalidJsonFormat", "Invalid JSON format:\n{0}"), FText::FromString(Error)));
		return FReply::Handled();
	}

	if (CurrentOutputPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("EmptyOutputPath", "Please specify an output path."));
		return FReply::Handled();
	}

	OnConfirmed.ExecuteIfBound(CurrentJson, CurrentOutputPath);

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SPenToUMGPreviewWindow::OnCancelClicked()
{
	OnCancelled.ExecuteIfBound();

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SPenToUMGPreviewWindow::OnFormatJsonClicked()
{
	CurrentJson = FormatJson(CurrentJson);
	if (JsonEditor.IsValid())
	{
		JsonEditor->SetText(FText::FromString(CurrentJson));
	}
	// 格式化后也刷新预览
	RefreshUMGPreview();
	return FReply::Handled();
}

FReply SPenToUMGPreviewWindow::OnPreviewUMGClicked()
{
	RefreshUMGPreview();
	return FReply::Handled();
}

void SPenToUMGPreviewWindow::OnJsonTextChanged(const FText& NewText)
{
	CurrentJson = NewText.ToString();

	// 解析控件列表和预览刷新统一走防抖
	SchedulePreviewRefresh();
}

void SPenToUMGPreviewWindow::SchedulePreviewRefresh()
{
	// 防抖：连续输入只触发一次刷新
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
		GEditor->GetTimerManager()->SetTimer(
			PreviewRefreshTimerHandle,
			FTimerDelegate::CreateSP(this, &SPenToUMGPreviewWindow::RefreshUMGPreview),
			PenToUMGConstants::JsonEditDebounceSeconds,
			false  // 不循环
		);
	}
}

void SPenToUMGPreviewWindow::OnOutputPathChanged(const FText& NewText)
{
	CurrentOutputPath = NewText.ToString();
}

bool SPenToUMGPreviewWindow::ValidateJson(const FString& JsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Failed to parse JSON. Please check the syntax.");
		return false;
	}

	if (!JsonObject->HasField(TEXT("RootWidget")))
	{
		OutError = TEXT("Missing required field: RootWidget");
		return false;
	}

	const TSharedPtr<FJsonObject>* RootWidget;
	if (!JsonObject->TryGetObjectField(TEXT("RootWidget"), RootWidget))
	{
		OutError = TEXT("RootWidget must be an object");
		return false;
	}

	if (!(*RootWidget)->HasField(TEXT("Type")))
	{
		OutError = TEXT("RootWidget missing required field: Type");
		return false;
	}

	return true;
}

FString SPenToUMGPreviewWindow::FormatJson(const FString& JsonString)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		FString FormattedJson;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&FormattedJson);

		if (FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
		{
			return FormattedJson;
		}
	}

	return JsonString;
}

void SPenToUMGPreviewWindow::CleanupPreviewWidget()
{
	if (PreviewWidget.Get())
	{
		PreviewWidget->RemoveFromParent();
		// 显式清理 WidgetTree 防止悬挂引用
		if (PreviewWidget->WidgetTree)
		{
			PreviewWidget->WidgetTree->RootWidget = nullptr;
		}
		PreviewWidget.Reset();
	}
}

void SPenToUMGPreviewWindow::RefreshUMGPreview()
{
	// 防抖后同步刷新控件调整面板
	if (!bUpdatingFromAdjustPanel)
	{
		ParseWidgetsFromJson();
	}

	if (!UMGPreviewContainer.IsValid())
	{
		return;
	}

	FString Error;
	if (!ValidateJson(CurrentJson, Error))
	{
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("PreviewError", "JSON Error:\n{0}"), FText::FromString(Error)))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)))
				.AutoWrapText(true)
			]
		);
		return;
	}

	// 显示加载状态，让UI有机会先刷新
	UMGPreviewContainer->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
		.Padding(8)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Parsing", "Parsing JSON..."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
	);

	// 延迟执行实际解析，让UI先显示"加载中"
	TWeakPtr<SPenToUMGPreviewWindow> WeakSelf = SharedThis(this);
	FString JsonToParse = CurrentJson;

	// 使用 AsyncTask 在下一帧执行，避免 OnPreTick 返回值问题
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, JsonToParse]()
	{
		TSharedPtr<SPenToUMGPreviewWindow> Self = WeakSelf.Pin();
		if (Self.IsValid())
		{
			Self->DoRefreshPreview(JsonToParse);
		}
	});
}

void SPenToUMGPreviewWindow::DoRefreshPreview(const FString& JsonString)
{
	if (!UMGPreviewContainer.IsValid())
	{
		return;
	}

	// 先解析 JSON 计算设计尺寸
	// 使用固定的标准设计尺寸（960x540）作为默认值，确保占位文字缩放效果一致
	// CalculateDesignSize 会根据实际内容计算正确的尺寸
	FUMGWidgetData TempWidgetData;
	FString ParseError;
	FVector2D DefaultSize(960.0f, 540.0f);
	FVector2D DesignSize = DefaultSize;
	if (FUMGJsonParser::ParseJson(JsonString, TempWidgetData, &ParseError))
	{
		DesignSize = FUMGWidgetUtils::CalculateDesignSize(TempWidgetData.RootWidget, DefaultSize);
	}

	UE_LOG(LogPenToUMG, Verbose, TEXT("[预览窗口] DoRefreshPreview - DefaultSize: (%.1f, %.1f), DesignSize: (%.1f, %.1f), GlobalScale: %.2f"),
		DefaultSize.X, DefaultSize.Y,
		DesignSize.X, DesignSize.Y,
		GlobalScaleFactor);

	// 应用全局缩放因子
	FVector2D ScaledDesignSize = DesignSize * GlobalScaleFactor;

	CleanupPreviewWidget();

	UUserWidget* NewWidget = CreatePreviewWidgetFromJson(TempWidgetData);
	if (NewWidget)
	{
		PreviewWidget.Reset(NewWidget);  // 使用 TStrongObjectPtr 持有

		UE_LOG(LogPenToUMG, Verbose, TEXT("[预览窗口] Widget创建成功, WidgetTree=%s, RootWidget=%s"),
			NewWidget->WidgetTree ? TEXT("有效") : TEXT("空"),
			NewWidget->WidgetTree && NewWidget->WidgetTree->RootWidget ? *NewWidget->WidgetTree->RootWidget->GetName() : TEXT("空"));

		TSharedRef<SWidget> SlateWidget = NewWidget->TakeWidget();

		UE_LOG(LogPenToUMG, Verbose, TEXT("[预览窗口] SlateWidget类型: %s, 可见: %s"),
			*SlateWidget->GetTypeAsString(),
			SlateWidget->GetVisibility().IsVisible() ? TEXT("是") : TEXT("否"));

		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			.Padding(4)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(ScaledDesignSize.X)
					.HeightOverride(ScaledDesignSize.Y)
					[
						SlateWidget
					]
				]
			]
		);
	}
	else
	{
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.05f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PreviewFailed", "Failed to create preview widget.\nCheck the Output Log for details."))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)))
				.AutoWrapText(true)
			]
		);
	}
}

UUserWidget* SPenToUMGPreviewWindow::CreatePreviewWidgetFromJson(const FUMGWidgetData& WidgetData)
{
	// 下载远程图片为临时纹理，填充 TextureMap
	TMap<FString, UTexture2D*> TextureMap;
	FPenRemoteImageImporter::ResolveTransientTextures(WidgetData.RootWidget, TextureMap);

	// 使用 RF_Transient 标记，明确告知 GC 这是临时对象
	UUserWidget* TempWidget = NewObject<UPreviewUserWidget>(GetTransientPackage(), UPreviewUserWidget::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create UUserWidget"));
		return nullptr;
	}

	TempWidget->WidgetTree = NewObject<UWidgetTree>(TempWidget, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget->WidgetTree)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create WidgetTree"));
		return nullptr;
	}

	// 使用带纹理映射的 Widget 创建函数（预览时直接绑定临时纹理）
	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(TempWidget->WidgetTree, WidgetData.RootWidget, nullptr, &TextureMap);
	if (!RootWidget)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create root widget of type: %s"), *WidgetData.RootWidget.Type);
		return nullptr;
	}

	TempWidget->WidgetTree->RootWidget = RootWidget;

	// 注意：不调用 Initialize()，因为预览 Widget 不需要完整初始化
	// Initialize() 需要 PlayerController 等运行时对象，在编辑器预览中不可用
	return TempWidget;
}

TSharedRef<SWidget> SPenToUMGPreviewWindow::BuildWidgetAdjustPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WidgetAdjust", "Widget Adjust"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(WidgetAdjustList, SVerticalBox)
				]
			]
		];
}

void SPenToUMGPreviewWindow::ParseWidgetsFromJson()
{
	WidgetAdjustItems.Empty();

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CurrentJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	// 递归解析Widget树
	TFunction<void(const TSharedPtr<FJsonObject>&, const FString&, int32)> ParseWidget;
	ParseWidget = [this, &ParseWidget](const TSharedPtr<FJsonObject>& WidgetObj, const FString& Path, int32 Depth)
	{
		if (!WidgetObj.IsValid()) return;

		FWidgetAdjustData Item;
		WidgetObj->TryGetStringField(TEXT("Name"), Item.Name);
		WidgetObj->TryGetStringField(TEXT("Type"), Item.Type);
		Item.JsonPath = Path;
		Item.Depth = Depth;

		// 解析 Slot.Offsets 和 Anchors
		const TSharedPtr<FJsonObject>* SlotObj;
		if (WidgetObj->TryGetObjectField(TEXT("Slot"), SlotObj))
		{
			const TSharedPtr<FJsonObject>* OffsetsObj;
			if ((*SlotObj)->TryGetObjectField(TEXT("Offsets"), OffsetsObj))
			{
				(*OffsetsObj)->TryGetNumberField(TEXT("Left"), Item.PosX);
				(*OffsetsObj)->TryGetNumberField(TEXT("Top"), Item.PosY);
				(*OffsetsObj)->TryGetNumberField(TEXT("Right"), Item.Width);
				(*OffsetsObj)->TryGetNumberField(TEXT("Bottom"), Item.Height);
			}

			// 检查是否为拉伸锚点
			const TSharedPtr<FJsonObject>* AnchorsObj;
			if ((*SlotObj)->TryGetObjectField(TEXT("Anchors"), AnchorsObj))
			{
				const TSharedPtr<FJsonObject>* MinObj;
				const TSharedPtr<FJsonObject>* MaxObj;
				if ((*AnchorsObj)->TryGetObjectField(TEXT("Min"), MinObj) &&
					(*AnchorsObj)->TryGetObjectField(TEXT("Max"), MaxObj))
				{
					double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
					(*MinObj)->TryGetNumberField(TEXT("X"), MinX);
					(*MinObj)->TryGetNumberField(TEXT("Y"), MinY);
					(*MaxObj)->TryGetNumberField(TEXT("X"), MaxX);
					(*MaxObj)->TryGetNumberField(TEXT("Y"), MaxY);
					Item.bStretchX = !FMath::IsNearlyEqual(MinX, MaxX);
					Item.bStretchY = !FMath::IsNearlyEqual(MinY, MaxY);
				}
			}
		}

		// 如果没有Name，使用Type作为显示名
		if (Item.Name.IsEmpty())
		{
			Item.Name = Item.Type;
		}

		WidgetAdjustItems.Add(Item);

		// 递归处理子控件
		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray;
		if (WidgetObj->TryGetArrayField(TEXT("Children"), ChildrenArray))
		{
			for (int32 i = 0; i < ChildrenArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& ChildObj = (*ChildrenArray)[i]->AsObject();
				FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *Path, i);
				ParseWidget(ChildObj, ChildPath, Depth + 1);
			}
		}
	};

	// 从 RootWidget 开始解析
	const TSharedPtr<FJsonObject>* RootWidget;
	if (JsonObject->TryGetObjectField(TEXT("RootWidget"), RootWidget))
	{
		ParseWidget(*RootWidget, TEXT("RootWidget"), 0);
	}

	RebuildWidgetAdjustList();
}

void SPenToUMGPreviewWindow::RebuildWidgetAdjustList()
{
	if (!WidgetAdjustList.IsValid()) return;

	WidgetAdjustList->ClearChildren();

	for (int32 i = 0; i < WidgetAdjustItems.Num(); ++i)
	{
		const FWidgetAdjustData& Item = WidgetAdjustItems[i];
		int32 WidgetIndex = i;

		// 计算缩进
		float LeftPadding = Item.Depth * 16.0f;

		WidgetAdjustList->AddSlot()
		.AutoHeight()
		.Padding(LeftPadding, 2, 0, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(4)
			[
				SNew(SVerticalBox)

				// 控件名称和类型
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Item.Name, *Item.Type)))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]

				// 位置: X, Y
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PosLabel", "Pos:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("XLabel", "X"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0)
					[
						SNew(SSpinBox<float>)
						.MinValue(-4096.0f)
						.MaxValue(4096.0f)
						.Delta(1.0f)
						.Value(Item.PosX)
						.OnValueChanged_Lambda([this, WidgetIndex](float NewValue)
						{
							OnWidgetPropertyChanged(WidgetIndex, TEXT("PosX"), NewValue);
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("YLabel", "Y"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0)
					[
						SNew(SSpinBox<float>)
						.MinValue(-4096.0f)
						.MaxValue(4096.0f)
						.Delta(1.0f)
						.Value(Item.PosY)
						.OnValueChanged_Lambda([this, WidgetIndex](float NewValue)
						{
							OnWidgetPropertyChanged(WidgetIndex, TEXT("PosY"), NewValue);
						})
					]
				]

				// 尺寸/边距: W,H (点锚点) 或 R,B (拉伸锚点)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 0)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(STextBlock)
						.Text(Item.bStretchX || Item.bStretchY
							? LOCTEXT("MarginLabel", "Margin:")
							: LOCTEXT("SizeLabel", "Size:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Item.bStretchX ? LOCTEXT("RLabel", "R") : LOCTEXT("WLabel", "W"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0)
					[
						SNew(SSpinBox<float>)
						.MinValue(Item.bStretchX ? 0.0f : 1.0f)
						.MaxValue(4096.0f)
						.Delta(1.0f)
						.Value(Item.Width)
						.OnValueChanged_Lambda([this, WidgetIndex](float NewValue)
						{
							OnWidgetPropertyChanged(WidgetIndex, TEXT("Width"), NewValue);
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(Item.bStretchY ? LOCTEXT("BLabel", "B") : LOCTEXT("HLabel", "H"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0)
					[
						SNew(SSpinBox<float>)
						.MinValue(Item.bStretchY ? 0.0f : 1.0f)
						.MaxValue(4096.0f)
						.Delta(1.0f)
						.Value(Item.Height)
						.OnValueChanged_Lambda([this, WidgetIndex](float NewValue)
						{
							OnWidgetPropertyChanged(WidgetIndex, TEXT("Height"), NewValue);
						})
					]
				]
			]
		];
	}
}

void SPenToUMGPreviewWindow::OnWidgetPropertyChanged(int32 WidgetIndex, const FString& PropertyName, float NewValue)
{
	if (!WidgetAdjustItems.IsValidIndex(WidgetIndex)) return;

	FWidgetAdjustData& Item = WidgetAdjustItems[WidgetIndex];

	if (PropertyName == TEXT("PosX")) Item.PosX = NewValue;
	else if (PropertyName == TEXT("PosY")) Item.PosY = NewValue;
	else if (PropertyName == TEXT("Width")) Item.Width = NewValue;
	else if (PropertyName == TEXT("Height")) Item.Height = NewValue;

	UpdateJsonFromAdjustData();
}

void SPenToUMGPreviewWindow::UpdateJsonFromAdjustData()
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CurrentJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	// 根据 JsonPath 更新每个控件的 Offsets
	for (const FWidgetAdjustData& Item : WidgetAdjustItems)
	{
		// 解析路径，找到对应的 JsonObject
		TSharedPtr<FJsonObject> TargetObj = JsonObject;
		TArray<FString> PathParts;
		Item.JsonPath.ParseIntoArray(PathParts, TEXT("."));

		for (const FString& Part : PathParts)
		{
			if (Part.Contains(TEXT("[")))
			{
				// 数组访问: Children[0]
				FString ArrayName = Part.Left(Part.Find(TEXT("[")));
				int32 Index = FCString::Atoi(*Part.Mid(Part.Find(TEXT("[")) + 1));

				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (TargetObj->TryGetArrayField(ArrayName, Arr) && Arr->IsValidIndex(Index))
				{
					TargetObj = (*Arr)[Index]->AsObject();
				}
				else
				{
					TargetObj = nullptr;
					break;
				}
			}
			else
			{
				// 对象访问
				const TSharedPtr<FJsonObject>* Obj;
				if (TargetObj->TryGetObjectField(Part, Obj))
				{
					TargetObj = *Obj;
				}
				else
				{
					TargetObj = nullptr;
					break;
				}
			}
		}

		if (!TargetObj.IsValid()) continue;

		// 更新 Slot.Offsets（使用 TryGetObjectField 避免警告）
		TSharedPtr<FJsonObject> SlotObj;
		const TSharedPtr<FJsonObject>* SlotObjPtr;
		if (TargetObj->TryGetObjectField(TEXT("Slot"), SlotObjPtr))
		{
			SlotObj = *SlotObjPtr;
		}
		else
		{
			SlotObj = MakeShareable(new FJsonObject());
			TargetObj->SetObjectField(TEXT("Slot"), SlotObj);
		}

		TSharedPtr<FJsonObject> OffsetsObj;
		const TSharedPtr<FJsonObject>* OffsetsObjPtr;
		if (SlotObj->TryGetObjectField(TEXT("Offsets"), OffsetsObjPtr))
		{
			OffsetsObj = *OffsetsObjPtr;
		}
		else
		{
			OffsetsObj = MakeShareable(new FJsonObject());
			SlotObj->SetObjectField(TEXT("Offsets"), OffsetsObj);
		}

		OffsetsObj->SetNumberField(TEXT("Left"), Item.PosX);
		OffsetsObj->SetNumberField(TEXT("Top"), Item.PosY);
		OffsetsObj->SetNumberField(TEXT("Right"), Item.Width);
		OffsetsObj->SetNumberField(TEXT("Bottom"), Item.Height);

		// 清零 Bounds，防止 LayoutCalculator 用 Bounds 重算覆盖用户修改的 Offsets
		TargetObj->RemoveField(TEXT("Bounds"));
	}

	// 序列化回JSON字符串
	FString NewJson;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewJson);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	CurrentJson = NewJson;

	// 更新JSON编辑器显示（设置标志防止重复解析）
	bUpdatingFromAdjustPanel = true;
	if (JsonEditor.IsValid())
	{
		JsonEditor->SetText(FText::FromString(CurrentJson));
	}
	bUpdatingFromAdjustPanel = false;

	// 刷新预览
	SchedulePreviewRefresh();
}

float SPenToUMGPreviewWindow::GetGlobalScaleFactor() const
{
	return GlobalScaleFactor;
}

void SPenToUMGPreviewWindow::OnGlobalScaleFactorChanged(float NewValue)
{
	GlobalScaleFactor = NewValue;
	RefreshUMGPreview();
}

FText SPenToUMGPreviewWindow::GetGlobalScaleFactorText() const
{
	return FText::FromString(FString::Printf(TEXT("%.1fx"), GlobalScaleFactor));
}

#undef LOCTEXT_NAMESPACE
