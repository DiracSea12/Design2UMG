// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "SDesign2UMGPreviewWindow.h"
#include "Async/Async.h"
#include "DesignImportPipeline.h"
#include "DesignJsonUtils.h"
#include "DesignPreviewMaterializationService.h"
#include "Design2UMGNotifications.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/UserWidget.h"

namespace
{
	constexpr float JsonEditDebounceSeconds = 0.5f;

	const FSlateBrush* GetAppStyleBrush(const FName StyleName)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return FAppStyle::Get().GetBrush(StyleName);
#else
		return FAppStyle::GetBrush(StyleName);
#endif
	}

	FSlateFontInfo GetAppStyleFont(const FName StyleName)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return FAppStyle::Get().GetFontStyle(StyleName);
#else
		return FAppStyle::GetFontStyle(StyleName);
#endif
	}

	const TCHAR* DescribeFileState(EDesign2UMGFileState State)
	{
		switch (State)
		{
		case EDesign2UMGFileState::Saved: return TEXT("saved");
		case EDesign2UMGFileState::Restored: return TEXT("restored");
		case EDesign2UMGFileState::Indeterminate: return TEXT("indeterminate");
		default: return TEXT("unchanged");
		}
	}
	const TCHAR* DescribeMemoryState(EDesign2UMGMemoryState State)
	{
		switch (State)
		{
		case EDesign2UMGMemoryState::Modified: return TEXT("modified");
		case EDesign2UMGMemoryState::Restored: return TEXT("restored");
		case EDesign2UMGMemoryState::RecoveryFailed: return TEXT("recovery failed");
		default: return TEXT("unchanged");
		}
	}

	class FDesignImportPipelinePreviewRefreshAnalyzer final : public IDesignPreviewRefreshAnalyzer
	{
	public:
		virtual FPreviewRefreshResult AnalyzePreview(const FPreviewRefreshInput& Input) override
		{
			FPreviewRefreshInput RefreshInput = Input;
			RefreshInput.bResolveImages = false;
			return DesignImportPipeline::AnalyzePreviewRefresh(RefreshInput);
		}
	};
}
#define LOCTEXT_NAMESPACE "SDesign2UMGPreviewWindow"

void SDesign2UMGPreviewWindow::Construct(const FArguments& InArgs)
{
	CurrentJson = InArgs._OriginalJson;
	OriginalData = InArgs._OriginalData;
	InitialImageResult = InArgs._InitialImageResult;
	CurrentImageResult = InitialImageResult;
	CurrentOutputPath = InArgs._OutputPath;
	ConversionWarnings = InArgs._Warnings;
	CachedMaterializationSettingsSnapshot = InArgs._InitialMaterializationSettingsSnapshot;
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
			.Font(GetAppStyleFont("BoldFont"))
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
		.BorderImage(GetAppStyleBrush("ToolPanel.GroupBorder"))
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
					.Text_Lambda([this]()
					{
						return bFitPreviewToWindow ? LOCTEXT("UMGPreviewFit", "UMG Preview (fit to window)") : LOCTEXT("UMGPreviewActualSize", "UMG Preview (1:1)");
					})
					.Font(GetAppStyleFont("BoldFont"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12, 0)
				[
					SNew(SButton)
					.IsEnabled_Lambda([this]() { return PreviewDesignFrame.IsValid(); })
					.OnClicked_Lambda([this]()
					{
						bFitPreviewToWindow = !bFitPreviewToWindow;
						UpdatePreviewViewport();
						return FReply::Handled();
					})
					.Text_Lambda([this]()
					{
						return bFitPreviewToWindow ? LOCTEXT("PreviewActualSize", "Original size (1:1)") : LOCTEXT("FitPreview", "Fit to window");
					})
					.ToolTipText(LOCTEXT("FitPreviewTooltip", "Switch between the whole design and original size with scrolling."))
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
					.BorderImage(GetAppStyleBrush("WhiteBrush"))
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
		.BorderImage(GetAppStyleBrush("ToolPanel.GroupBorder"))
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
					.Font(GetAppStyleFont("BoldFont"))
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
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
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
	.Padding(0, 0, 0, 6)
	[
		SNew(SBorder)
		.Visibility_Lambda([this]()
		{
			return CurrentImageResult.IsSet() && CurrentImageResult->HasFailures()
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		.BorderImage(GetAppStyleBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.25f, 0.18f, 0.0f, 1.0f))
		.Padding(FMargin(8, 4))
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!CurrentImageResult.IsSet())
				{
					return FText::GetEmpty();
				}
				FString WarningText = FString::Printf(
					TEXT("%d image(s) failed to resolve:"), CurrentImageResult->FailedCount);
				int32 DisplayedCount = 0;
				for (const FDesignImageDiagnostic& Diagnostic : CurrentImageResult->Diagnostics)
				{
					if (Diagnostic.Outcome != EDesignImageDiagnosticOutcome::Failed)
					{
						continue;
					}
					if (DisplayedCount++ >= 32)
					{
						continue;
					}
					WarningText += TEXT("\n  - ");
					WarningText += Diagnostic.Message.IsEmpty()
						? FString::Printf(TEXT("%s: %s"), LexToString(Diagnostic.Category), *Diagnostic.Reason)
						: Diagnostic.Message;
				}
				if (DisplayedCount > 32)
				{
					WarningText += FString::Printf(
						TEXT("\n%d more image failure detail(s) omitted"), DisplayedCount - 32);
				}
				return FText::FromString(WarningText);
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]
	];

	BottomBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(STextBlock)
		.Text_Lambda([this]() { return GenerationStatus; })
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.2f)))
	];

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

TOptional<FDesignPreviewMaterializationSettingsSnapshot>
SDesign2UMGPreviewWindow::ResolveMaterializationSettingsSnapshot()
{
	// No ambient generation-settings read here: the only caller is DoRefreshPreview, which runs
	// from a queued AsyncTask(GameThread) after every settings-snapshot RAII scope on the
	// launching stack has popped (the engine task graph only queues game-thread tasks, it never
	// executes them inline). The effective priority chain is therefore: the latest approved
	// snapshot, else the cached one seeded at window construction from the import flow's
	// narrowed snapshot - exactly the values this window materialized with before.
	if (LatestApprovedSnapshot.IsSet() && LatestApprovedSnapshot->MaterializationSettingsSnapshot.IsSet())
	{
		CachedMaterializationSettingsSnapshot =
			LatestApprovedSnapshot->MaterializationSettingsSnapshot;
		return CachedMaterializationSettingsSnapshot;
	}

	return CachedMaterializationSettingsSnapshot;
}

FReply SDesign2UMGPreviewWindow::OnGenerateClicked()
{
	if (CurrentOutputPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("EmptyOutputPath", "Please specify an output path."));
		return FReply::Handled();
	}

	if (!CanConfirmLatestSnapshot())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("RefreshRequiredBeforeGenerate", "Preview is stale or failed to refresh. Refresh must succeed before generating."));
		return FReply::Handled();
	}

	FPreviewGenerationRequest GenerationRequest;
	GenerationRequest.ApprovedSnapshot = LatestApprovedSnapshot.GetValue();
	GenerationRequest.GenerationRequest.SourceRequestId = GenerationRequest.ApprovedSnapshot.RequestId;
	GenerationRequest.GenerationRequest.OutputPath = CurrentOutputPath;
	GenerationRequest.GenerationRequest.RuntimeImageResult = GenerationRequest.ApprovedSnapshot.ImageResult;
	if (!OnConfirmed.IsBound())
	{
		GenerationStatus = LOCTEXT("NoConfirmationHandler", "Generation was not started: no confirmation handler is bound.");
		return FReply::Handled();
	}
	const FArtifactGenerationResult Result = OnConfirmed.Execute(GenerationRequest);
	if (!Result.bSucceeded)
	{
		FString Message;
		switch (Result.Commit.Status)
		{
		case EDesign2UMGCommitStatus::CommittedWithMetadataFailure:
			Message = TEXT("The asset was saved, but its generation metadata was not completed.");
			break;
		case EDesign2UMGCommitStatus::RollbackIncomplete:
			Message = TEXT("Generation failed and recovery is incomplete. Check the asset and in-memory state before retrying.");
			Message += FString::Printf(TEXT("\nAsset state: %s; metadata state: %s; memory state: %s."),
				DescribeFileState(Result.Commit.Asset), DescribeFileState(Result.Commit.Manifest), DescribeMemoryState(Result.Commit.Memory));
			break;
		case EDesign2UMGCommitStatus::Conflict:
			Message = TEXT("Generation stopped because of a conflict.");
			break;
		default:
			Message = Result.Commit.IsSuccess() ? TEXT("The asset was committed, but a requested output was not completed.") : TEXT("Generation did not complete.");
			break;
		}
		const FString AssetPath = Result.GeneratedAssetPath.IsEmpty() ? Result.Commit.AssetPath : Result.GeneratedAssetPath;
		if (!AssetPath.IsEmpty()) Message += TEXT("\nAsset: ") + AssetPath;
		if (!Result.ErrorMessage.IsEmpty()) Message += TEXT("\n") + Result.ErrorMessage;
		for (const auto& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (!Diagnostic.Message.IsEmpty()) Message += TEXT("\n") + Diagnostic.Message;
		}
		GenerationStatus = FText::FromString(Message);
		return FReply::Handled();
	}
	GenerationStatus = FText::GetEmpty();
	if (Result.Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation)
	{
		FString Summary = FString::Join(Result.Warnings, TEXT("\n"));
		for (const auto& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (!Diagnostic.Message.IsEmpty()) Summary += TEXT("\n") + Diagnostic.Message;
		}
		if (Summary.IsEmpty()) Summary = TEXT("Generation completed with degradation. Review the import diagnostics.");
		FDesign2UMGNotification::Warning(FText::FromString(Summary));
	}

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
	CurrentImageResult.Reset();
	InvalidateApprovedSnapshot();

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
	if (!DesignJsonUtils::TryParseObject(JsonString, JsonObject))
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
	if (DesignJsonUtils::TryParseObject(JsonString, JsonObject))
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
	PreviewDesignFrame.Reset();
	if (PreviewMaterializationService)
	{
		PreviewMaterializationService->Reset();
	}
}

void SDesign2UMGPreviewWindow::UpdatePreviewViewport()
{
	// Detach the current viewport before reusing its design frame in the other view.
	UMGPreviewContainer->SetContent(SNullWidget::NullWidget);
	TSharedRef<SWidget> Viewport = SNullWidget::NullWidget;
	if (bFitPreviewToWindow)
	{
		Viewport = SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::DownOnly)
			[
				PreviewDesignFrame.ToSharedRef()
			];
	}
	else
	{
		Viewport = SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					PreviewDesignFrame.ToSharedRef()
				]
			];
	}
	UMGPreviewContainer->SetContent(
		SNew(SBorder)
		.BorderImage(GetAppStyleBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
		.Padding(4)
		[
			Viewport
		]);
}

void SDesign2UMGPreviewWindow::InvalidateApprovedSnapshot()
{
	LatestApprovedSnapshot.Reset();
	bLatestApprovedSnapshotStale = true;
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
		CurrentImageResult.Reset();
		InvalidateApprovedSnapshot();
		CleanupPreviewWidget();
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
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

	InvalidateApprovedSnapshot();
	const uint64 RequestId = NextRefreshRequestId++;
	LatestRequestedRefreshId = RequestId;

	// Show loading state first.
	PreviewDesignFrame.Reset();
	UMGPreviewContainer->SetContent(
		SNew(SBorder)
		.BorderImage(GetAppStyleBrush("WhiteBrush"))
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
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, RequestId, JsonToParse]()
	{
		TSharedPtr<SDesign2UMGPreviewWindow> Self = WeakSelf.Pin();
		if (Self.IsValid())
		{
			Self->DoRefreshPreview(RequestId, JsonToParse);
		}
	});
}

void SDesign2UMGPreviewWindow::DoRefreshPreview(uint64 RequestId, const FString& JsonString)
{
	if (!UMGPreviewContainer.IsValid() || RequestId != LatestRequestedRefreshId)
	{
		return;
	}

	if (!PreviewRefreshAnalyzer.IsValid())
	{
		PreviewRefreshAnalyzer = MakeShared<FDesignImportPipelinePreviewRefreshAnalyzer>();
	}

	FPreviewRefreshInput RefreshInput;
	RefreshInput.RequestId = RequestId;
	RefreshInput.EditedJson = JsonString;
	RefreshInput.OriginalData = OriginalData;
	RefreshInput.InitialImageResult = InitialImageResult;
	RefreshInput.bResolveImages = false;
	const FPreviewRefreshResult RefreshResult = PreviewRefreshAnalyzer->AnalyzePreview(RefreshInput);
	if (RequestId != LatestRequestedRefreshId)
	{
		return;
	}

	if (!RefreshResult.bSucceeded)
	{
		CurrentImageResult.Reset();
		InvalidateApprovedSnapshot();
		CleanupPreviewWidget();
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("PreviewParseError", "Preview parse failed:\n{0}"), FText::FromString(RefreshResult.ParseError)))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)))
				.AutoWrapText(true)
			]
		);
		return;
	}

	CurrentImageResult = RefreshResult.ImageResult;

	if (!PreviewMaterializationService)
	{
		PreviewMaterializationService = MakeShared<FDesignPreviewMaterializationService>();
	}

	FPreviewApprovedSnapshot ApprovedSnapshot;
	ApprovedSnapshot.RequestId = RefreshResult.RequestId;
	ApprovedSnapshot.EditedJson = RefreshResult.EditedJson;
	ApprovedSnapshot.Data = RefreshResult.Data;
	ApprovedSnapshot.ImageResult = RefreshResult.ImageResult;
	ApprovedSnapshot.MaterializationSettingsSnapshot = ResolveMaterializationSettingsSnapshot();

	FDesignPreviewMaterializationRequest MaterializationRequest;
	MaterializationRequest.RequestId = RequestId;
	MaterializationRequest.ApprovedSnapshot = ApprovedSnapshot;

	FDesignPreviewMaterializationResult MaterializationResult;
	const bool bMaterialized = PreviewMaterializationService->MaterializePreview(MaterializationRequest, MaterializationResult);
	if (RequestId != LatestRequestedRefreshId)
	{
		return;
	}
	if (bMaterialized && MaterializationResult.bSucceeded)
	{
		UUserWidget* MaterializedPreviewWidget = PreviewMaterializationService->GetMaterializedPreviewWidget();
		if (!MaterializedPreviewWidget)
		{
			InvalidateApprovedSnapshot();
			CleanupPreviewWidget();
			UMGPreviewContainer->SetContent(
				SNew(SBorder)
				.BorderImage(GetAppStyleBrush("WhiteBrush"))
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

		LatestApprovedSnapshot = ApprovedSnapshot;
		bLatestApprovedSnapshotStale = false;

		TSharedRef<SWidget> SlateWidget = MaterializedPreviewWidget->TakeWidget();

		SAssignNew(PreviewDesignFrame, SBox)
			.WidthOverride(MaterializationResult.PreviewDesignSize.X)
			.HeightOverride(MaterializationResult.PreviewDesignSize.Y)
			[
				SlateWidget
			];
		UpdatePreviewViewport();
	}
	else
	{
		InvalidateApprovedSnapshot();
		CleanupPreviewWidget();
		const FText FailureMessage = MaterializationResult.ErrorMessage.IsEmpty()
			? LOCTEXT("PreviewFailed", "Failed to create preview widget.\nCheck the Output Log for details.")
			: FText::Format(LOCTEXT("PreviewResourceLimit", "Preview stopped because image resources exceed the limit.\n{0}\n\nReduce image sizes or preview fewer nodes, then try again."),
				FText::FromString(MaterializationResult.ErrorMessage));
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.05f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(FailureMessage)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)))
				.AutoWrapText(true)
			]
		);
		if (!MaterializationResult.ErrorMessage.IsEmpty())
		{
			const FText FailureTitle = LOCTEXT("PreviewResourceLimitTitle", "Design2UMG - Preview resource limit");
#if UE_VERSION_OLDER_THAN(5, 3, 0)
			FMessageDialog::Open(EAppMsgType::Ok, FailureMessage, &FailureTitle);
#else
			FMessageDialog::Open(EAppMsgType::Ok, FailureMessage,
				FailureTitle);
#endif
		}
	}
}

#undef LOCTEXT_NAMESPACE
