// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "SDesign2UMGPreviewWindow.h"
#include "DesignImportDialogs.h"
#include "DesignImportPlanning.h"
#include "Async/Async.h"
#include "DesignImportPipeline.h"
#include "DesignJsonUtils.h"
#include "DesignPreviewMaterializationService.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "UMGJsonSerializer.h"
#include "SPencilFontTool.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/StringTableRegistry.h"

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
#include "Widgets/Input/SCheckBox.h"
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

	FText DescribeFileState(EDesign2UMGFileState State)
	{
		switch (State)
		{
		case EDesign2UMGFileState::Saved: return LOCTABLE("Design2UMGPreviewWindow", "StateSaved");
		case EDesign2UMGFileState::Restored: return LOCTABLE("Design2UMGPreviewWindow", "StateRestored");
		case EDesign2UMGFileState::Indeterminate: return LOCTABLE("Design2UMGPreviewWindow", "StateIndeterminate");
		default: return LOCTABLE("Design2UMGPreviewWindow", "StateUnchanged");
		}
	}
	FText DescribeMemoryState(EDesign2UMGMemoryState State)
	{
		switch (State)
		{
		case EDesign2UMGMemoryState::Modified: return LOCTABLE("Design2UMGPreviewWindow", "StateModified");
		case EDesign2UMGMemoryState::Restored: return LOCTABLE("Design2UMGPreviewWindow", "StateRestored");
		case EDesign2UMGMemoryState::RecoveryFailed: return LOCTABLE("Design2UMGPreviewWindow", "StateRecoveryFailed");
		default: return LOCTABLE("Design2UMGPreviewWindow", "StateUnchanged");
		}
	}

	class FDesignImportPipelinePreviewRefreshAnalyzer final : public IDesignPreviewRefreshAnalyzer
	{
	public:
		virtual FPreviewRefreshResult AnalyzePreview(const FPreviewRefreshInput& Input) override
		{
			FPreviewRefreshInput RefreshInput = Input;
			RefreshInput.bResolveImages = false;
			return DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
		}
	};
}

void SDesign2UMGPreviewWindow::Construct(const FArguments& InArgs)
{
	CurrentJson = InArgs._OriginalJson;
	OriginalData = InArgs._OriginalData;
	InitialImageResult = InArgs._InitialImageResult;
	CurrentImageResult = InitialImageResult;
	CurrentOutputPath = InArgs._OutputPath;
	SourceFilePath = InArgs._SourceFilePath;
	ConversionWarnings = InArgs._Warnings;
	CachedMaterializationSettingsSnapshot = InArgs._InitialMaterializationSettingsSnapshot;
	OnConfirmed = InArgs._OnConfirmed;
	OnCancelled = InArgs._OnCancelled;
	OnReconvert = InArgs._OnReconvert;
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
	if (!FStringTableRegistry::Get().FindStringTable(TEXT("Design2UMGPreviewWindow")).IsValid())
	{
		FStringTableRegistry::Get().Internal_LocTableFromFile(
			TEXT("Design2UMGPreviewWindow"), TEXT("SDesign2UMGPreviewWindow"),
			TEXT("Resources/PreviewWindowText.csv"),
			IPluginManager::Get().FindPlugin(TEXT("Design2UMG"))->GetBaseDir());
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8)
		[
			SNew(STextBlock)
			.Text(LOCTABLE("Design2UMGPreviewWindow", "PreviewTitle"))
			.Font(GetAppStyleFont("BoldFont"))
			.AutoWrapText(true)
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

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)
		[
			SAssignNew(FontTool, SPencilFontTool)
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
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return bFitPreviewToWindow ? LOCTABLE("Design2UMGPreviewWindow", "UMGPreviewFit") : LOCTABLE("Design2UMGPreviewWindow", "UMGPreviewActualSize");
					})
					.Font(GetAppStyleFont("BoldFont"))
					.AutoWrapText(true)
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
						return bFitPreviewToWindow ? LOCTABLE("Design2UMGPreviewWindow", "PreviewActualSize") : LOCTABLE("Design2UMGPreviewWindow", "FitPreview");
					})
					.ToolTipText(LOCTABLE("Design2UMGPreviewWindow", "FitPreviewTooltip"))
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
						.Text(LOCTABLE("Design2UMGPreviewWindow", "PreviewReady"))
						.AutoWrapText(true)
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
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTABLE("Design2UMGPreviewWindow", "JsonEditor"))
					.Font(GetAppStyleFont("BoldFont"))
					.AutoWrapText(true)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTABLE("Design2UMGPreviewWindow", "FormatJson"))
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
	TSharedRef<SVerticalBox> DiagnosticsBox = SNew(SVerticalBox);

	if (ConversionWarnings.Num() > 0)
	{
		DiagnosticsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 6)
		[
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.25f, 0.18f, 0.0f, 1.0f))
			.Padding(FMargin(8, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTABLE("Design2UMGPreviewWindow", "ConversionWarningsSummary"), FText::AsNumber(ConversionWarnings.Num())))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Join(ConversionWarnings, TEXT("\n\n"))))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.AutoWrapText(true)
				]
			]
		];
	}

	DiagnosticsBox->AddSlot()
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
				FString WarningText = FText::Format(
					LOCTABLE("Design2UMGPreviewWindow", "ImageFailuresSummary"),
					FText::AsNumber(CurrentImageResult->FailedCount)).ToString();
				for (const FDesignImageDiagnostic& Diagnostic : CurrentImageResult->Diagnostics)
				{
					if (Diagnostic.Outcome != EDesignImageDiagnosticOutcome::Failed)
					{
						continue;
					}
					const FString Detail = Diagnostic.Message.IsEmpty()
						? FText::Format(LOCTABLE("Design2UMGPreviewWindow", "ImageDiagnosticReason"),
							FText::FromString(LexToString(Diagnostic.Category)), FText::FromString(Diagnostic.Reason)).ToString()
						: Diagnostic.Message;
					WarningText += FText::Format(LOCTABLE("Design2UMGPreviewWindow", "ImageDiagnosticItem"), FText::FromString(Detail)).ToString();
				}
				return FText::FromString(WarningText);
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]
	];

	DiagnosticsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(STextBlock)
		.Text_Lambda([this]() { return GenerationStatus; })
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.2f)))
	];

	BottomBox->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.MaxDesiredHeight(160.0f)
		[
			SNew(SScrollBox)
			.Clipping(EWidgetClipping::ClipToBounds)
			+ SScrollBox::Slot()
			[
				DiagnosticsBox
			]
		]
	];

	if (OnReconvert.IsBound())
	{
		BottomBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 8)
		[
			SNew(SCheckBox)
			.IsChecked(GetDefault<UDesign2UMGSettings>()->bPreserveSourceNodes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.OnCheckStateChanged(this, &SDesign2UMGPreviewWindow::OnPreserveSourceNodesChanged)
			.ToolTipText(LOCTABLE("Design2UMGPreviewWindow", "PreserveSourceNodesTooltip"))
			[
				SNew(STextBlock)
				.Text(LOCTABLE("Design2UMGPreviewWindow", "PreserveSourceNodes"))
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
				.Text(LOCTABLE("Design2UMGPreviewWindow", "OutputPath"))
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
			.Text(LOCTABLE("Design2UMGPreviewWindow", "Cancel"))
			.OnClicked(this, &SDesign2UMGPreviewWindow::OnCancelClicked)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0)
		[
			SNew(SButton)
			.Text(LOCTABLE("Design2UMGPreviewWindow", "Generate"))
			.OnClicked(this, &SDesign2UMGPreviewWindow::OnGenerateClicked)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		]
	];

	return BottomBox;
}

SDesign2UMGPreviewWindow::~SDesign2UMGPreviewWindow()
{
	CancelFontToolInstall();
	// Clear debounce timer.
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(PreviewRefreshTimerHandle);
	}
	CleanupPreviewWidget();
}

void SDesign2UMGPreviewWindow::CancelFontToolInstall()
{
	if (FontTool) FontTool->CancelInstall();
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

FString SDesign2UMGPreviewWindow::FormatPreviewFailure(const FString& ErrorMessage) const
{
	if (OriginalData.IsValid())
	{
		return DesignImportDialogs::FormatImportResult(*OriginalData, ErrorMessage, false, SourceFilePath);
	}
	const FUMGWidgetData EmptyData;
	return DesignImportDialogs::FormatImportResult(EmptyData, ErrorMessage, false, SourceFilePath);
}

FReply SDesign2UMGPreviewWindow::OnGenerateClicked()
{
	if (CurrentOutputPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTABLE("Design2UMGPreviewWindow", "EmptyOutputPath"));
		return FReply::Handled();
	}

	if (!CanConfirmLatestSnapshot())
	{
		DesignImportDialogs::ShowImportProblem(GenerationStatus.IsEmpty()
			? LOCTABLE("Design2UMGPreviewWindow", "RefreshRequiredBeforeGenerate").ToString()
			: GenerationStatus.ToString());
		return FReply::Handled();
	}

	FPreviewGenerationRequest GenerationRequest;
	GenerationRequest.ApprovedSnapshot = LatestApprovedSnapshot.GetValue();
	GenerationRequest.GenerationRequest.SourceRequestId = GenerationRequest.ApprovedSnapshot.RequestId;
	GenerationRequest.GenerationRequest.OutputPath = CurrentOutputPath;
	GenerationRequest.GenerationRequest.RuntimeImageResult = GenerationRequest.ApprovedSnapshot.ImageResult;
	if (!OnConfirmed.IsBound())
	{
		GenerationStatus = LOCTABLE("Design2UMGPreviewWindow", "NoConfirmationHandler");
		return FReply::Handled();
	}
	const FArtifactGenerationResult Result = OnConfirmed.Execute(GenerationRequest);
	if (!Result.bSucceeded)
	{
		FString Message;
		switch (Result.Commit.Status)
		{
		case EDesign2UMGCommitStatus::CommittedWithMetadataFailure:
			Message = LOCTABLE("Design2UMGPreviewWindow", "GenerationMetadataIncomplete").ToString();
			break;
		case EDesign2UMGCommitStatus::RollbackIncomplete:
			Message = LOCTABLE("Design2UMGPreviewWindow", "GenerationRollbackIncomplete").ToString();
			Message += FText::Format(LOCTABLE("Design2UMGPreviewWindow", "GenerationRecoveryStates"),
				DescribeFileState(Result.Commit.Asset), DescribeFileState(Result.Commit.Manifest), DescribeMemoryState(Result.Commit.Memory)).ToString();
			break;
		case EDesign2UMGCommitStatus::Conflict:
			Message = LOCTABLE("Design2UMGPreviewWindow", "GenerationConflict").ToString();
			break;
		default:
			Message = (Result.Commit.IsSuccess() ? LOCTABLE("Design2UMGPreviewWindow", "GenerationOutputIncomplete") : LOCTABLE("Design2UMGPreviewWindow", "GenerationFailed")).ToString();
			break;
		}
		const FString AssetPath = Result.GeneratedAssetPath.IsEmpty() ? Result.Commit.AssetPath : Result.GeneratedAssetPath;
		if (!AssetPath.IsEmpty()) Message += FText::Format(LOCTABLE("Design2UMGPreviewWindow", "GenerationAssetPath"), FText::FromString(AssetPath)).ToString();
		if (!Result.ErrorMessage.IsEmpty()) Message += TEXT("\n") + Result.ErrorMessage;
		for (const auto& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (!Diagnostic.Message.IsEmpty()) Message += TEXT("\n") + Diagnostic.Message;
		}
		Message = DesignImportDialogs::FormatImportResult(GenerationRequest.ApprovedSnapshot.Data, Message, false, SourceFilePath);
		GenerationStatus = FText::FromString(Message);
		DesignImportDialogs::ShowImportProblem(Message);
		return FReply::Handled();
	}
	GenerationStatus = FText::GetEmpty();
	if (Result.Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation
		|| !Result.Warnings.IsEmpty() || !GenerationRequest.ApprovedSnapshot.Data.Warnings.IsEmpty()
		|| GenerationRequest.ApprovedSnapshot.ImageResult.HasFailures())
	{
		FString Issues = FString::Join(Result.Warnings, TEXT("\n"));
		for (const auto& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (!Diagnostic.Message.IsEmpty()) Issues += TEXT("\n") + Diagnostic.Message;
		}
		if (GenerationRequest.ApprovedSnapshot.ImageResult.HasFailures())
			Issues += TEXT("\n") + DesignImportDialogs::FormatImageIssues(GenerationRequest.ApprovedSnapshot.ImageResult);
		if (Result.Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation)
		{
			if (Issues.IsEmpty()) Issues = LOCTABLE("Design2UMGPreviewWindow", "GenerationDegraded").ToString();
			FDesign2UMGNotification::Warning(FText::FromString(Issues));
		}
		DesignImportDialogs::ShowImportProblem(DesignImportDialogs::FormatImportResult(
			GenerationRequest.ApprovedSnapshot.Data, Issues, true, SourceFilePath));
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
	CancelFontToolInstall();
	OnCancelled.ExecuteIfBound();

	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void SDesign2UMGPreviewWindow::OnPreserveSourceNodesChanged(ECheckBoxState State)
{
	UDesign2UMGSettings* Settings = GetMutableDefault<UDesign2UMGSettings>();
	Settings->bPreserveSourceNodes = State == ECheckBoxState::Checked;
	Settings->SaveConfig();
	FUMGWidgetData Data;
	FString Error;
	if (!OnReconvert.Execute(Settings->bPreserveSourceNodes, Data, Error))
	{
		GenerationStatus = FText::FromString(FormatPreviewFailure(Error));
		return;
	}
	OriginalData = MakeShared<FUMGWidgetData>(MoveTemp(Data));
	CurrentJson = FormatJson(FUMGJsonSerializer::Serialize(*OriginalData));
	if (JsonEditor.IsValid()) JsonEditor->SetText(FText::FromString(CurrentJson));
	CurrentImageResult.Reset();
	InvalidateApprovedSnapshot();
	RefreshUMGPreview();
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
		OutError = LOCTABLE("Design2UMGPreviewWindow", "InvalidJson").ToString();
		return false;
	}

	if (!JsonObject->HasField(TEXT("RootWidget")))
	{
		OutError = LOCTABLE("Design2UMGPreviewWindow", "MissingRootWidget").ToString();
		return false;
	}

	const TSharedPtr<FJsonObject>* RootWidget;
	if (!JsonObject->TryGetObjectField(TEXT("RootWidget"), RootWidget))
	{
		OutError = LOCTABLE("Design2UMGPreviewWindow", "RootWidgetNotObject").ToString();
		return false;
	}

	if (!(*RootWidget)->HasField(TEXT("Type")))
	{
		OutError = LOCTABLE("Design2UMGPreviewWindow", "MissingRootWidgetType").ToString();
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
	GenerationStatus = FText::GetEmpty();
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
		GenerationStatus = FText::FromString(FormatPreviewFailure(Error));
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(GenerationStatus)
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
			.Text(LOCTABLE("Design2UMGPreviewWindow", "Parsing"))
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
		GenerationStatus = FText::FromString(FormatPreviewFailure(RefreshResult.ParseError));
		UMGPreviewContainer->SetContent(
			SNew(SBorder)
			.BorderImage(GetAppStyleBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.02f, 1.0f))
			.Padding(8)
			[
				SNew(STextBlock)
				.Text(GenerationStatus)
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
					.Text(LOCTABLE("Design2UMGPreviewWindow", "PreviewWidgetMissing"))
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
		const FString FailureDetails = DesignImportDialogs::FormatImportResult(
		ApprovedSnapshot.Data, MaterializationResult.ErrorMessage, false, SourceFilePath);
		const FText FailureMessage = FText::FromString(FailureDetails);
	GenerationStatus = FailureMessage;
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
			DesignImportDialogs::ShowImportProblem(FailureDetails);
		}
	}
}
