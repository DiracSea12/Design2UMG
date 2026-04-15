// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "SDesign2UMGPreviewWindow.h"
#include "DesignImportPipeline.h"
#include "DesignPreviewMaterializationService.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/UserWidget.h"

namespace
{
	constexpr float JsonEditDebounceSeconds = 0.5f;

	class FDesignImportPipelinePreviewRefreshAnalyzer final : public IDesignPreviewRefreshAnalyzer
	{
	public:
		virtual bool AnalyzePreviewJson(
			const FString& JsonString,
			FUMGWidgetData& OutWidgetData,
			FString& OutImageReport,
			FString& OutError) override
		{
			FPreviewRefreshInput RefreshInput;
			RefreshInput.EditedJson = JsonString;
			RefreshInput.bResolveImages = false;

			const FPreviewRefreshResult RefreshResult = DesignImportPipeline::AnalyzePreviewRefresh(RefreshInput);
			if (!RefreshResult.bSucceeded)
			{
				OutError = RefreshResult.ParseError;
				return false;
			}

			OutWidgetData = RefreshResult.Data;
			OutImageReport = RefreshResult.ImageReport;
			return true;
		}
	};
}
#define LOCTEXT_NAMESPACE "SDesign2UMGPreviewWindow"

void SDesign2UMGPreviewWindow::Construct(const FArguments& InArgs)
{
	CurrentJson = InArgs._OriginalJson;
	CurrentOutputPath = InArgs._OutputPath;
	ConversionWarnings = InArgs._Warnings;
	OnConfirmed = InArgs._OnConfirmed;
	OnCancelled = InArgs._OnCancelled;
	PreviewRefreshAnalyzer = InArgs._PreviewRefreshAnalyzer;
	PreviewMaterializationService = InArgs._PreviewMaterializationService;

	if (!PreviewRefreshAnalyzer.IsValid())
	{
		PreviewRefreshAnalyzer = MakeShared<FDesignImportPipelinePreviewRefreshAnalyzer>();
	}
	if (!PreviewMaterializationService.IsValid())
	{
		PreviewMaterializationService = MakeShared<FDesignPreviewMaterializationService>();
	}

	CurrentJson = FormatJson(CurrentJson);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewTitle", "Design2UMG Preview - Review and edit the generated JSON before creating the Widget Blueprint"))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 0, 8, 8)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.68f)[ BuildUMGPreviewPanel() ]
			+ SSplitter::Slot().Value(0.32f)[ BuildJsonEditorPanel() ]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			BuildBottomBar()
		]
	];

	// Initial preview build.
	RefreshUMGPreview();
}

TSharedRef<SWidget> SDesign2UMGPreviewWindow::BuildUMGPreviewPanel()
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
					.Text(LOCTEXT("UMGPreview", "UMG Preview (1:1)"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
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
						.Text(LOCTEXT("PreviewReady", "Preview refreshes automatically when JSON changes"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						.Justification(ETextJustify::Center)
					]
				]
			]
		];
}

TSharedRef<SWidget> SDesign2UMGPreviewWindow::BuildJsonEditorPanel()
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
					.OnClicked(this, &SDesign2UMGPreviewWindow::OnFormatJsonClicked)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				SAssignNew(JsonEditor, SMultiLineEditableTextBox)
				.Text(FText::FromString(CurrentJson))
				.OnTextChanged(this, &SDesign2UMGPreviewWindow::OnJsonTextChanged)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
				.AutoWrapText(false)
				.Margin(4)
			]
		];
}

TSharedRef<SWidget> SDesign2UMGPreviewWindow::BuildBottomBar()
{
	TSharedRef<SVerticalBox> BottomBox = SNew(SVerticalBox);

	// Degradation warning summary.
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
				.OnTextChanged(this, &SDesign2UMGPreviewWindow::OnOutputPathChanged)
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
			.OnClicked(this, &SDesign2UMGPreviewWindow::OnCancelClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Generate", "Generate Widget Blueprint"))
			.OnClicked(this, &SDesign2UMGPreviewWindow::OnGenerateClicked)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		]
	];

	return BottomBox;
}

SDesign2UMGPreviewWindow::~SDesign2UMGPreviewWindow()
{
	// Clear debounce timer.
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
	}
	CleanupPreviewWidget();
}

FReply SDesign2UMGPreviewWindow::OnGenerateClicked()
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

FReply SDesign2UMGPreviewWindow::OnCancelClicked()
{
	OnCancelled.ExecuteIfBound();

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SDesign2UMGPreviewWindow::OnFormatJsonClicked()
{
	CurrentJson = FormatJson(CurrentJson);
	if (JsonEditor.IsValid())
	{
		JsonEditor->SetText(FText::FromString(CurrentJson));
	}
	// Refresh after formatting.
	RefreshUMGPreview();
	return FReply::Handled();
}

void SDesign2UMGPreviewWindow::OnJsonTextChanged(const FText& NewText)
{
	CurrentJson = NewText.ToString();

	// Keep preview refresh debounced while editing.
	SchedulePreviewRefresh();
}

void SDesign2UMGPreviewWindow::SchedulePreviewRefresh()
{
	// Debounce continuous typing.
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
		GEditor->GetTimerManager()->SetTimer(
			PreviewRefreshTimerHandle,
			FTimerDelegate::CreateSP(this, &SDesign2UMGPreviewWindow::RefreshUMGPreview),
			JsonEditDebounceSeconds,
			false  // one-shot timer
		);
	}
}

void SDesign2UMGPreviewWindow::OnOutputPathChanged(const FText& NewText)
{
	CurrentOutputPath = NewText.ToString();
}

bool SDesign2UMGPreviewWindow::ValidateJson(const FString& JsonString, FString& OutError)
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

FString SDesign2UMGPreviewWindow::FormatJson(const FString& JsonString)
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

void SDesign2UMGPreviewWindow::CleanupPreviewWidget()
{
	if (PreviewMaterializationService)
	{
		PreviewMaterializationService->Reset();
	}
}

void SDesign2UMGPreviewWindow::RefreshUMGPreview()
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

	// Show loading state first.
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

	// Delay parsing to the next tick so loading state can paint.
	TWeakPtr<SDesign2UMGPreviewWindow> WeakSelf = SharedThis(this);
	FString JsonToParse = CurrentJson;

	// Run on next frame.
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, JsonToParse]()
	{
		TSharedPtr<SDesign2UMGPreviewWindow> Self = WeakSelf.Pin();
		if (Self.IsValid())
		{
			Self->DoRefreshPreview(JsonToParse);
		}
	});
}

void SDesign2UMGPreviewWindow::DoRefreshPreview(const FString& JsonString)
{
	if (!UMGPreviewContainer.IsValid())
	{
		return;
	}

	FUMGWidgetData TempWidgetData;
	FString ImageReport;
	FString ParseError;
	if (!PreviewRefreshAnalyzer.IsValid())
	{
		PreviewRefreshAnalyzer = MakeShared<FDesignImportPipelinePreviewRefreshAnalyzer>();
	}

	if (!PreviewRefreshAnalyzer->AnalyzePreviewJson(JsonString, TempWidgetData, ImageReport, ParseError))
	{
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("PreviewParseError", "Preview parse failed:\n{0}"), FText::FromString(ParseError)))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)))
				.AutoWrapText(true)
			]
		);
		return;
	}

	if (!PreviewMaterializationService)
	{
		PreviewMaterializationService = MakeShared<FDesignPreviewMaterializationService>();
	}

	FDesignPreviewMaterializationResult MaterializationResult;
	if (PreviewMaterializationService->MaterializePreview(TempWidgetData, MaterializationResult))
	{
		UUserWidget* MaterializedPreviewWidget = PreviewMaterializationService->GetMaterializedPreviewWidget();
		if (!MaterializedPreviewWidget)
		{
			UMGPreviewContainer->SetContent(
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.1f, 0.05f, 0.02f, 1.0f))
				.Padding(8)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewWidgetMissing", "Preview materialization completed without a widget.\nCheck the Output Log for details."))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)))
					.AutoWrapText(true)
				]
			);
			return;
		}

		TSharedRef<SWidget> SlateWidget = MaterializedPreviewWidget->TakeWidget();

		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			.Padding(4)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
					SNew(SBox)
						.WidthOverride(MaterializationResult.PreviewDesignSize.X)
						.HeightOverride(MaterializationResult.PreviewDesignSize.Y)
						[
							SlateWidget
						]
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

#undef LOCTEXT_NAMESPACE


