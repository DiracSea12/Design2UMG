// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "SDesign2UMGPreviewWindow.h"
#include "UMGWidgetUtils.h"
#include "UMGJsonParser.h"
#include "PenRemoteImageImporter.h"
#include "Design2UMGLog.h"
#include "Design2UMGConstants.h"
#include "Design2UMGSettings.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
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
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "PreviewUserWidget.h"
#define LOCTEXT_NAMESPACE "SDesign2UMGPreviewWindow"

namespace
{
	FVector2D ResolvePreviewDesignSize(const FUMGWidgetData& WidgetData)
	{
		const FBounds& RootBounds = WidgetData.RootWidget.Bounds;
		if (RootBounds.W > 0.0f && RootBounds.H > 0.0f)
		{
			return FVector2D(RootBounds.W, RootBounds.H);
		}

		const FVector2D CalculatedSize = FUMGWidgetUtils::CalculateDesignSize(WidgetData.RootWidget, FVector2D::ZeroVector);
		if (CalculatedSize.X > 0.0f && CalculatedSize.Y > 0.0f)
		{
			return CalculatedSize;
		}

		const FIntPoint FallbackSize = UDesign2UMGSettings::Get()->GetDesignSize();
		return FVector2D((float)FallbackSize.X, (float)FallbackSize.Y);
	}
}

void SDesign2UMGPreviewWindow::Construct(const FArguments& InArgs)
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
			Design2UMGConstants::JsonEditDebounceSeconds,
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
	if (PreviewWidget.Get())
	{
		PreviewWidget->RemoveFromParent();
		// Explicitly clear WidgetTree to avoid stale references.
		if (PreviewWidget->WidgetTree)
		{
			PreviewWidget->WidgetTree->RootWidget = nullptr;
		}
		PreviewWidget.Reset();
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
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(JsonString, TempWidgetData, &ParseError))
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

	const FVector2D DesignSize = ResolvePreviewDesignSize(TempWidgetData);

	CleanupPreviewWidget();

	UUserWidget* NewWidget = CreatePreviewWidgetFromJson(TempWidgetData);
	if (NewWidget)
	{
		PreviewWidget.Reset(NewWidget);
		TSharedRef<SWidget> SlateWidget = NewWidget->TakeWidget();

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
						.WidthOverride(DesignSize.X)
						.HeightOverride(DesignSize.Y)
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
UUserWidget* SDesign2UMGPreviewWindow::CreatePreviewWidgetFromJson(const FUMGWidgetData& WidgetData)
{
	// Download remote images as transient textures.
	TMap<FString, UTexture2D*> TextureMap;
	FPenRemoteImageImporter::ResolveTransientTextures(WidgetData.RootWidget, TextureMap);

	// Keep the same fixer path as final generation.
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	const FVector2D PreviewSize = ResolvePreviewDesignSize(WidgetData);
	const FIntPoint DesignSize(FMath::Max(1, FMath::RoundToInt(PreviewSize.X)), FMath::Max(1, FMath::RoundToInt(PreviewSize.Y)));
	FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);

	// Mark transient so GC treats it as preview-only object.
	UUserWidget* TempWidget = NewObject<UPreviewUserWidget>(GetTransientPackage(), UPreviewUserWidget::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create UUserWidget"));
		return nullptr;
	}

	TempWidget->WidgetTree = NewObject<UWidgetTree>(TempWidget, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	if (!TempWidget->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create WidgetTree"));
		return nullptr;
	}

	// Build preview widget tree with transient texture map.
	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(TempWidget->WidgetTree, FixedRoot, nullptr, &TextureMap);
	if (!RootWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget of type: %s"), *WidgetData.RootWidget.Type);
		return nullptr;
	}

	TempWidget->WidgetTree->RootWidget = RootWidget;

	// Do not call Initialize() in editor preview; runtime objects are unavailable.
	return TempWidget;
}

#undef LOCTEXT_NAMESPACE

