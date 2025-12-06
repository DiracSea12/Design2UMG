// Copyright Epic Games, Inc. All Rights Reserved.

#include "SImageToUMGPreviewWindow.h"
#include "UMGWidgetUtils.h"
#include "UMGJsonParser.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
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
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/ContentWidget.h"

#define LOCTEXT_NAMESPACE "SImageToUMGPreviewWindow"

void SImageToUMGPreviewWindow::Construct(const FArguments& InArgs)
{
	CurrentJson = InArgs._OriginalJson;
	CurrentOutputPath = InArgs._OutputPath;
	OnConfirmed = InArgs._OnConfirmed;
	OnCancelled = InArgs._OnCancelled;
	SourceImageData = InArgs._SourceImageData;
	SourceImageSize = InArgs._SourceImageSize;
	AIImageSize = InArgs._AIImageSize;

	// 计算坐标缩放因子：AI坐标 * CoordScale = 原始坐标
	if (AIImageSize.X > 0 && AIImageSize.Y > 0)
	{
		CoordScale = static_cast<float>(SourceImageSize.X) / AIImageSize.X;
	}

	CurrentJson = FormatJson(CurrentJson);

	UTexture2D* Texture = InArgs._SourceTexture.Get();
	if (Texture)
	{
		ImageBrush = MakeShareable(new FSlateBrush());
		ImageBrush->SetResourceObject(Texture);
		ImageBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		ImageBrush->DrawAs = ESlateBrushDrawType::Image;
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewTitle", "ImageToUMG Preview - Review and edit the generated JSON before creating the Widget Blueprint"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 0, 8, 8)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.2f)[ BuildSourceImagePanel() ]
			+ SSplitter::Slot().Value(0.5f)[ BuildUMGPreviewPanel() ]
			+ SSplitter::Slot().Value(0.3f)[ BuildJsonEditorPanel() ]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			BuildBottomBar()
		]
	];

	// 自动刷新预览
	RefreshUMGPreview();
}

TSharedRef<SWidget> SImageToUMGPreviewWindow::BuildSourceImagePanel()
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
				.Text(LOCTEXT("SourceImage", "Source Image"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						ImageBrush.IsValid()
							? StaticCastSharedRef<SWidget>(
								SNew(SScaleBox)
								.Stretch(EStretch::ScaleToFit)
								[
									SNew(SImage)
									.Image(ImageBrush.Get())
								]
							)
							: StaticCastSharedRef<SWidget>(
								SNew(STextBlock)
								.Text(LOCTEXT("NoImage", "No image available"))
							)
					]
				]
			]
		];
}

TSharedRef<SWidget> SImageToUMGPreviewWindow::BuildUMGPreviewPanel()
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
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshPreview", "Refresh Preview"))
					.OnClicked(this, &SImageToUMGPreviewWindow::OnPreviewUMGClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
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
			]
		];
}

TSharedRef<SWidget> SImageToUMGPreviewWindow::BuildJsonEditorPanel()
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
					.OnClicked(this, &SImageToUMGPreviewWindow::OnFormatJsonClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(JsonEditor, SMultiLineEditableTextBox)
				.Text(FText::FromString(CurrentJson))
				.OnTextChanged(this, &SImageToUMGPreviewWindow::OnJsonTextChanged)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
				.AutoWrapText(false)
				.Margin(4)
			]
		];
}

TSharedRef<SWidget> SImageToUMGPreviewWindow::BuildBottomBar()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
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
				.OnTextChanged(this, &SImageToUMGPreviewWindow::OnOutputPathChanged)
			]
		]

		+ SVerticalBox::Slot()
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
				.OnClicked(this, &SImageToUMGPreviewWindow::OnCancelClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Generate", "Generate Widget Blueprint"))
				.OnClicked(this, &SImageToUMGPreviewWindow::OnGenerateClicked)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			]
		];
}

SImageToUMGPreviewWindow::~SImageToUMGPreviewWindow()
{
	// 清理防抖定时器
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
	}
	CleanupPreviewWidget();
}

FReply SImageToUMGPreviewWindow::OnGenerateClicked()
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

	OnConfirmed.ExecuteIfBound(CurrentJson, CurrentOutputPath, SourceImageData, SourceImageSize, CoordScale);

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SImageToUMGPreviewWindow::OnCancelClicked()
{
	OnCancelled.ExecuteIfBound();

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SImageToUMGPreviewWindow::OnFormatJsonClicked()
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

FReply SImageToUMGPreviewWindow::OnPreviewUMGClicked()
{
	RefreshUMGPreview();
	return FReply::Handled();
}

void SImageToUMGPreviewWindow::OnJsonTextChanged(const FText& NewText)
{
	CurrentJson = NewText.ToString();
	SchedulePreviewRefresh();
}

void SImageToUMGPreviewWindow::SchedulePreviewRefresh()
{
	// 防抖：500ms 内的连续输入只触发一次刷新
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
		GEditor->GetTimerManager()->SetTimer(
			PreviewRefreshTimerHandle,
			FTimerDelegate::CreateSP(this, &SImageToUMGPreviewWindow::RefreshUMGPreview),
			0.5f,  // 500ms 防抖延迟
			false  // 不循环
		);
	}
}

void SImageToUMGPreviewWindow::OnOutputPathChanged(const FText& NewText)
{
	CurrentOutputPath = NewText.ToString();
}

bool SImageToUMGPreviewWindow::ValidateJson(const FString& JsonString, FString& OutError)
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

FString SImageToUMGPreviewWindow::FormatJson(const FString& JsonString)
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

void SImageToUMGPreviewWindow::CleanupPreviewWidget()
{
	if (PreviewWidget.IsValid())
	{
		PreviewWidget->RemoveFromParent();
		PreviewWidget.Reset();
	}
}

void SImageToUMGPreviewWindow::RefreshUMGPreview()
{
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

	CleanupPreviewWidget();

	UUserWidget* NewWidget = CreatePreviewWidgetFromJson(CurrentJson);
	if (NewWidget)
	{
		PreviewWidget = NewWidget;
		TSharedRef<SWidget> SlateWidget = NewWidget->TakeWidget();

		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			.Padding(4)
			[
				SlateWidget
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

UUserWidget* SImageToUMGPreviewWindow::CreatePreviewWidgetFromJson(const FString& JsonString)
{
	FUMGWidgetData WidgetData;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(JsonString, WidgetData, &ParseError))
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("Failed to parse JSON for preview: %s"), *ParseError);
		return nullptr;
	}

	// 从源图像裁剪实际纹理用于预览
	TArray<FUMGImageAsset>& ImageAssets = WidgetData.ImageAssets;

	// 兜底：AI没有返回ImageAssets时，从Widget节点的位置信息生成
	if (ImageAssets.Num() == 0)
	{
		FUMGWidgetUtils::GenerateImageAssetsFromWidgets(WidgetData.RootWidget, ImageAssets);
	}

	if (ImageAssets.Num() > 0 && SourceImageData.Num() > 0)
	{
		TArray<FProcessedImageAsset> PreviewAssets;
		FImageAssetProcessor::ProcessImageAssetsForPreview(
			ImageAssets, SourceImageData, PreviewAssets, PreviewTextures, CoordScale);
		FUMGWidgetUtils::BindImageAssets(WidgetData.RootWidget, PreviewAssets);
	}

	UUserWidget* TempWidget = NewObject<UUserWidget>(GetTransientPackage(), UUserWidget::StaticClass());
	if (!TempWidget)
	{
		return nullptr;
	}

	TempWidget->WidgetTree = NewObject<UWidgetTree>(TempWidget, UWidgetTree::StaticClass());
	if (!TempWidget->WidgetTree)
	{
		return nullptr;
	}

	// 使用统一的 Widget 创建函数
	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(TempWidget->WidgetTree, WidgetData.RootWidget);
	if (RootWidget)
	{
		TempWidget->WidgetTree->RootWidget = RootWidget;
		TempWidget->Initialize();
		return TempWidget;
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
