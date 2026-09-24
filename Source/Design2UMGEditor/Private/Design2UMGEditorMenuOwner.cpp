// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorMenuOwner.h"

#include "Design2UMGEditorImportOrchestrator.h"
#include "DesignImportDefaults.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignImportPipeline.h"

#include "Containers/Ticker.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	using namespace Design2UMGEditorImportOrchestrator;

	void OpenPenFileDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			return;
		}

		TArray<FString> Files;
		const bool bOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import Pencil Design File"),
			FPaths::ProjectDir(),
			TEXT(""),
			TEXT("Pencil Design Files (*.pen)|*.pen"),
			EFileDialogFlags::None,
			Files);

		if (!bOpened || Files.Num() == 0)
		{
			return;
		}

		FPenImportRequest Request;
		Request.FilePath = Files[0];
		RunPenImport(Request);
	}

	void OpenLocalFigmaJsonDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			return;
		}

		TArray<FString> Files;
		const bool bOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import Local Figma Export (design.json)"),
			FPaths::ProjectDir(),
			TEXT("design.json"),
			TEXT("Figma Export JSON (design.json)|design.json|JSON Files (*.json)|*.json"),
			EFileDialogFlags::None,
			Files);

		if (!bOpened || Files.Num() == 0)
		{
			return;
		}

		FLocalFigmaImportRequest Request;
		Request.JsonFilePath = Files[0];
		Request.OutputPath = DesignImportDefaults::FigmaOutputPath;
		RunLocalFigmaImport(Request);
	}

	void ShowFigmaImportDialog()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("FigmaImportDialogTitle", "Import from Figma"))
			.ClientSize(FVector2D(500.0f, 160.0f))
			.SupportsMinimize(false)
			.SupportsMaximize(false);

		TSharedPtr<SEditableTextBox> UrlBox;
		TSharedPtr<SEditableTextBox> TokenBox;
		TWeakPtr<SWindow> WeakWindow = Window;

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("FigmaUrl", "Figma URL:")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SAssignNew(UrlBox, SEditableTextBox).HintText(LOCTEXT("FigmaUrlHint", "https://www.figma.com/design/... or file key")) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("FigmaToken", "Access Token:")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SAssignNew(TokenBox, SEditableTextBox).IsPassword(true).HintText(LOCTEXT("FigmaTokenHint", "Personal Access Token")) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8).HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("FigmaImportBtn", "Import"))
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.OnClicked_Lambda([UrlBox, TokenBox, WeakWindow]()
				{
					const FString Url = UrlBox->GetText().ToString().TrimStartAndEnd();
					const FString Token = TokenBox->GetText().ToString().TrimStartAndEnd();
					if (Url.IsEmpty() || Token.IsEmpty())
					{
						FDesign2UMGNotification::Warning(LOCTEXT("FigmaImportMissingInput", "Design2UMG: Figma URL and access token are required."));
						return FReply::Handled();
					}

					if (const TSharedPtr<SWindow> WindowToClose = WeakWindow.Pin())
					{
						WindowToClose->RequestDestroyWindow();
					}

					RunFigmaImport(Url, Token);
					return FReply::Handled();
				})
			]);

		FSlateApplication::Get().AddWindow(Window);
	}

	// The reimport entry collects exactly two inputs: the new design source (a local Figma-exported design.json)
	// and the target asset (the generated WBP .uasset or its .d2u.json sidecar), then calls DesignImportPipeline::Reimport.
	// On full conflict it asks once whether to generate a sibling copy (explicit user choice; never implicit).
	void OpenReimportDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			return;
		}

		TArray<FString> SourceFiles;
		const bool bSourceOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Reimport Design (Design2UMG): pick the new design source"),
			FPaths::ProjectDir(),
			TEXT("design.json"),
			TEXT("Figma Export JSON (design.json)|design.json|JSON Files (*.json)|*.json"),
			EFileDialogFlags::None,
			SourceFiles);
		if (!bSourceOpened || SourceFiles.Num() == 0)
		{
			return;
		}

		TArray<FString> TargetFiles;
		const bool bTargetOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Reimport Design (Design2UMG): pick the generated Widget Blueprint"),
			FPaths::ProjectContentDir(),
			TEXT(""),
			TEXT("D2U Generated Asset (*.d2u.json;*.uasset)|*.d2u.json;*.uasset"),
			EFileDialogFlags::None,
			TargetFiles);
		if (!bTargetOpened || TargetFiles.Num() == 0)
		{
			return;
		}

		// The sidecar (.d2u.json) lives next to the asset with the same name: normalize to .uasset, then to a package path.
		FString AssetFilePath = TargetFiles[0];
		if (AssetFilePath.EndsWith(TEXT(".d2u.json")))
		{
			AssetFilePath = AssetFilePath.LeftChop(9) + FPackageName::GetAssetPackageExtension();
		}
		FString TargetPackage;
		if (!FPackageName::TryConvertFilenameToLongPackageName(AssetFilePath, TargetPackage))
		{
			FDesign2UMGNotification::Error(LOCTEXT(
				"ReimportBadTarget",
				"Design2UMG: could not resolve the selected file to a project asset path"));
			return;
		}

		FDesignDocument Document;
		FString LoadError;
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourceFiles[0], Document, LoadError))
		{
			FDesign2UMGNotification::Error(FText::Format(
				LOCTEXT("ReimportSourceLoadFailed", "Design2UMG: failed to load design source: {0}"),
				FText::FromString(LoadError)));
			return;
		}

		FDesignReimportRequest Request;
		Request.TargetAssetPath = TargetPackage;
		FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
		if (Result.Commit.Status == EDesign2UMGCommitStatus::Conflict)
		{
			const EAppReturnType::Type Choice = FMessageDialog::Open(
				EAppMsgCategory::Warning,
				EAppMsgType::YesNo,
				EAppReturnType::No,
				LOCTEXT(
					"ReimportFullConflictAsk",
					"Reimport hit a full conflict: the asset and its manifest relationship is broken, so the asset was preserved.\nGenerate a side-by-side resolution copy from the new design? (the original stays untouched)"),
				LOCTEXT("ReimportFullConflictTitle", "Design2UMG Reimport: full conflict"));
			if (Choice == EAppReturnType::Yes)
			{
				Request.bGenerateResolutionCopy = true;
				Result = DesignImportPipeline::Reimport(Document, Request);
			}
		}

		const bool bHasCopyResult = Result.ResolutionCopyCommit.IsSet();
		const FDesign2UMGCommitResult& Commit = bHasCopyResult ? Result.ResolutionCopyCommit.GetValue() : Result.Commit;
		if (Commit.IsSuccess())
		{
			const FText Message = bHasCopyResult
				? FText::Format(LOCTEXT("ReimportCopyDone", "Design2UMG: resolution copy generated at {0}"), FText::FromString(Commit.AssetPath))
				: LOCTEXT("ReimportDone", "Design2UMG: reimport applied (see Output Log for the node report)");
			if (Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation) FDesign2UMGNotification::Warning(Message);
			else FDesign2UMGNotification::Success(Message);
		}
		else if (Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure)
		{
			FDesign2UMGNotification::Error(FText::Format(LOCTEXT("ReimportMetadataFailed",
				"Design2UMG: asset saved at {0}, but its manifest was not updated; see Output Log before retrying"), FText::FromString(Commit.AssetPath)));
		}
		else if (Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete)
		{
			FDesign2UMGNotification::Error(LOCTEXT("ReimportRecoveryIncomplete",
				"Design2UMG: reimport failed and recovery is incomplete; inspect Output Log before saving or retrying"));
		}
		else if (Commit.Status == EDesign2UMGCommitStatus::Conflict)
		{
			FDesign2UMGNotification::Warning(LOCTEXT("ReimportFullConflictKept",
				"Design2UMG: reimport hit a full conflict; the asset kept its previous state (see Output Log for details)"));
		}
		else
		{
			FDesign2UMGNotification::Error(Commit.Memory == EDesign2UMGMemoryState::Modified
				? LOCTEXT("ReimportNotCommittedMemoryRetained", "Design2UMG: reimport not committed; unsaved in-memory changes remain")
				: LOCTEXT("ReimportNotCommitted", "Design2UMG: reimport not committed; see Output Log for details"));
		}
	}

	void ShowWelcomeDialog(UDesign2UMGSettings* Settings)
	{
		if (!FSlateApplication::IsInitialized() || !Settings)
		{
			return;
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("WelcomeTitle", "Welcome to Design2UMG"))
			.ClientSize(FVector2D(450.0f, 300.0f))
			.SupportsMinimize(false)
			.SupportsMaximize(false);

		TWeakPtr<SWindow> WeakWindow = Window;
		TSharedPtr<SEditableTextBox> OutputPathBox;

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(12.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WelcomeMsg", "Configure your default import settings:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(STextBlock).Text(LOCTEXT("WelcomeImagePath", "Image Import Path:")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SAssignNew(OutputPathBox, SEditableTextBox).Text(FText::FromString(Settings->RemoteImageImportPath)) ]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 8.0f)
			[
				SNew(SButton).HAlign(HAlign_Center)
				.Text(LOCTEXT("WelcomeOk", "Get Started"))
				.OnClicked_Lambda([WeakWindow, Settings, OutputPathBox]()
				{
					Settings->RemoteImageImportPath = OutputPathBox->GetText().ToString();
					Settings->bFirstRunCompleted = true;
					Settings->SaveConfig();
					if (const TSharedPtr<SWindow> WindowToClose = WeakWindow.Pin())
					{
						WindowToClose->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]);

		FSlateApplication::Get().AddWindow(Window);
	}
}

namespace Design2UMGEditorMenuOwner
{
	FDelegateHandle RegisterMenus()
	{
		return UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu"));
			UToolMenu* SubMenu = Menu->AddSubMenu(
				TEXT("Design2UMG"),
				NAME_None,
				TEXT("Design2UMG"),
				LOCTEXT("Design2UMGMenu", "Design2UMG"));

			FToolMenuSection& Section = SubMenu->AddSection(TEXT("Import"), LOCTEXT("ImportSection", "Import"));

			Section.AddMenuEntry(
				TEXT("ImportPenFile"),
				LOCTEXT("ImportPenFile", "Import .pen File..."),
				LOCTEXT("ImportPenFileTooltip", "Open a .pen file and convert to Widget Blueprint"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					OpenPenFileDialog();
				})));

			Section.AddMenuEntry(
				TEXT("ConnectToPencil"),
				LOCTEXT("ConnectToPencil", "Connect to Pencil..."),
				LOCTEXT("ConnectToPencilTooltip", "Connect to a running Pencil instance via MCP"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					ConnectToPencilAndImport();
				})));

			Section.AddMenuEntry(
				TEXT("ImportFromFigma"),
				LOCTEXT("ImportFromFigma", "Import from Figma..."),
				LOCTEXT("ImportFromFigmaTooltip", "Import a Figma design file and convert to Widget Blueprint"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					ShowFigmaImportDialog();
				})));

			Section.AddMenuEntry(
				TEXT("ImportLocalFigma"),
				LOCTEXT("ImportLocalFigma", "Import Local Figma Export..."),
				LOCTEXT("ImportLocalFigmaTooltip", "Import a local Figma export design.json + images folder"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					OpenLocalFigmaJsonDialog();
				})));

			FToolMenuSection& ReimportSection = SubMenu->AddSection(
				TEXT("Reimport"), LOCTEXT("ReimportSection", "Reimport"));

			ReimportSection.AddMenuEntry(
				TEXT("ReimportDesign"),
				LOCTEXT("ReimportDesign", "Reimport Design..."),
				LOCTEXT("ReimportDesignTooltip", "Safely update a D2U-generated Widget Blueprint from a newer design source"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					OpenReimportDialog();
				})));
		}));
	}

	void UnregisterMenus(const FDelegateHandle StartupHandle, void* Owner)
	{
		UToolMenus::UnRegisterStartupCallback(StartupHandle);
		UToolMenus::UnregisterOwner(Owner);
	}

	void MaybeShowWelcomeDialog()
	{
		if (FApp::IsUnattended())
		{
			return;
		}

		UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		if (!Settings || Settings->bFirstRunCompleted || !FSlateApplication::IsInitialized())
		{
			return;
		}

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Settings](float) -> bool
			{
				if (!Settings->bFirstRunCompleted)
				{
					ShowWelcomeDialog(Settings);
				}
				return false;
			}),
			1.0f);
	}
}

#undef LOCTEXT_NAMESPACE
