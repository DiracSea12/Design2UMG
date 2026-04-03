// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorActions.h"

#include "Automation/Design2UMGAutomationRender.h"
#include "Design2UMGConverter.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignImportDialogs.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "DesignPathRasterizer.h"
#include "FigmaHttpClient.h"
#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "PenMCPBridge.h"
#include "PencilConnector.h"
#include "ProceduralShapeGenerator.h"
#include "UMGWidgetUtils.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "IDesktopPlatform.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <atomic>

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	const TCHAR* DefaultPenOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");
	const TCHAR* DefaultFigmaOutputPath = TEXT("/Game/Design2UMG/FigmaConvert_Output");

	TUniquePtr<FPencilConnector> GConnector;
	TUniquePtr<FFigmaHttpClient> GFigmaClient;
	using FDesignSourceConvertFunc = TFunction<bool(const FString& RawJson, FUMGWidgetData& OutData)>;

	struct FPenConvertCommandArgs
	{
		FString NodesPath;
		FString VarsPath;
		FString TargetNodeId;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
	};

	struct FImportCommandOptions
	{
		FString OutputPath = DefaultPenOutputPath;
		FString TargetNodeId;
		bool bUsePreview = true;
		bool bRenderPng = true;
	};

	void ApplyOutputPreviewArg(const FString& Arg, FString& OutputPath, bool& bUsePreview);

	TMap<FName, FDesignSourceConvertFunc>& GetDesignSourceRegistry()
	{
		static TMap<FName, FDesignSourceConvertFunc> Registry;
		return Registry;
	}

	void RegisterDesignSource(FName SourceId, FDesignSourceConvertFunc Func)
	{
		if (GetDesignSourceRegistry().Contains(SourceId))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignSourceRegistry: Overwriting existing source '%s'"), *SourceId.ToString());
		}

		GetDesignSourceRegistry().Add(SourceId, MoveTemp(Func));
	}

	void UnregisterDesignSource(FName SourceId)
	{
		GetDesignSourceRegistry().Remove(SourceId);
	}

	FDesignImportRequest MakeImportRequest(
		const FString& OutputPath,
		const FText& PreviewWindowTitle,
		const bool bUsePreview,
		const EDesignBlueprintGenerationMode GenerationMode,
		const bool bResolveImages = true)
	{
		FDesignImportRequest Request;
		Request.OutputPath = OutputPath;
		Request.PreviewWindowTitle = PreviewWindowTitle;
		Request.bUsePreview = bUsePreview;
		Request.bResolveImages = bResolveImages;
		Request.Generation.Mode = GenerationMode;
		return Request;
	}

	FDesignImportRequest MakePenImportRequest(
		const FString& OutputPath,
		const bool bUsePreview,
		const bool bRenderPng)
	{
		return MakeImportRequest(
			OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview"),
			bUsePreview,
			bRenderPng ? EDesignBlueprintGenerationMode::BlueprintAndPng : EDesignBlueprintGenerationMode::BlueprintOnly);
	}

	void RequestExitIfUnattended(const TCHAR* Message)
	{
		if (!FApp::IsUnattended())
		{
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), Message);
		FPlatformMisc::RequestExit(false);
	}

	bool LoadTextFileOrLog(const FString& FilePath, FString& OutContents)
	{
		if (FFileHelper::LoadFileToString(OutContents, *FilePath))
		{
			return true;
		}

		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}

	bool ParsePenConvertCommandArgs(const TArray<FString>& Args, FPenConvertCommandArgs& Out)
	{
		if (Args.Num() < 2)
		{
			return false;
		}

		Out.NodesPath = Args[0];
		Out.VarsPath = Args[1];

		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			const FString& Arg = Args[Index];
			if (Arg.StartsWith(TEXT("--")))
			{
				ApplyOutputPreviewArg(Arg, Out.OutputPath, Out.bUsePreview);
			}
			else if (Out.TargetNodeId.IsEmpty())
			{
				Out.TargetNodeId = Arg;
			}
		}

		return true;
	}

	void ApplyOutputPreviewArg(const FString& Arg, FString& OutputPath, bool& bUsePreview)
	{
		if (Arg.StartsWith(TEXT("--out=")))
		{
			OutputPath = Arg.RightChop(6);
		}
		else if (Arg == TEXT("--preview"))
		{
			bUsePreview = true;
		}
		else if (Arg == TEXT("--no-preview"))
		{
			bUsePreview = false;
		}
	}

	void ParsePenImportCommandArgs(const TArray<FString>& Args, const int32 StartIndex, FImportCommandOptions& Out)
	{
		for (int32 Index = StartIndex; Index < Args.Num(); ++Index)
		{
			const FString& Arg = Args[Index];
			if (Arg.StartsWith(TEXT("--node=")))
			{
				Out.TargetNodeId = Arg.RightChop(7);
			}
			else if (Arg == TEXT("--no-png"))
			{
				Out.bRenderPng = false;
			}
			else if (Arg == TEXT("--png"))
			{
				Out.bRenderPng = true;
			}
			else
			{
				ApplyOutputPreviewArg(Arg, Out.OutputPath, Out.bUsePreview);
			}
		}
	}

	void ProcessMCPData(const FString& NodesJson, const FString& VarsJson, const FString& TargetNodeId, const FString& OutputPath)
	{
		FUMGWidgetData Data;
		if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, Data, TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: ConvertFromMCP failed"));
			return;
		}

		DesignImportPipeline::Execute(Data, MakePenImportRequest(OutputPath, true, true));
	}

	TArray<FDesignFrameInfo> ExtractFramesFromMCPResult(const FString& NodesJson)
	{
		TArray<FDesignFrameInfo> Result;
		TArray<TSharedPtr<FJsonValue>> Arr;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
		if (!FJsonSerializer::Deserialize(Reader, Arr))
		{
			return Result;
		}

		for (const TSharedPtr<FJsonValue>& Val : Arr)
		{
			const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			FDesignFrameInfo Info;
			Obj->TryGetStringField(TEXT("id"), Info.Id);
			Obj->TryGetStringField(TEXT("name"), Info.Name);
			if (Info.Name.IsEmpty())
			{
				Info.Name = Info.Id;
			}

			double Width = 0.0;
			double Height = 0.0;
			Obj->TryGetNumberField(TEXT("width"), Width);
			Obj->TryGetNumberField(TEXT("height"), Height);
			Info.Width = static_cast<float>(Width);
			Info.Height = static_cast<float>(Height);
			Result.Add(MoveTemp(Info));
		}

		return Result;
	}

	void ShowNotification(const FText& Text, const SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}

	void ExecuteFrameImport(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		TFunction<void(const FString&)> ImportFrame)
	{
		if (Frames.Num() == 1)
		{
			ImportFrame(FString());
			return;
		}

		DesignImportDialogs::ShowFrameSelector(
			Frames,
			WindowTitle,
			HintText,
			[ImportFrame = MoveTemp(ImportFrame)](const FString& FrameId) mutable
			{
				ImportFrame(FrameId);
			});
	}

	void OnMCPDataReady(const FString& NodesJson, const FString& VarsJson)
	{
		const TArray<FDesignFrameInfo> Frames = ExtractFramesFromMCPResult(NodesJson);
		if (Frames.Num() == 0)
		{
			ShowNotification(LOCTEXT("NoFrames", "Design2UMG: No frames found in Pencil document"), SNotificationItem::CS_Fail);
			return;
		}

		ExecuteFrameImport(
			Frames,
			LOCTEXT("PencilFrameSelectorTitle", "Select Frame from Pencil"),
			LOCTEXT("PencilFrameSelectorHint", "Multiple frames found. Double-click to import:"),
			[CapturedNodes = NodesJson, CapturedVars = VarsJson](const FString& FrameId)
			{
				ProcessMCPData(CapturedNodes, CapturedVars, FrameId, DefaultPenOutputPath);
			});
	}

	void ConnectToPencilAndImport()
	{
		static std::atomic<bool> bInProgress{false};
		bool bExpected = false;
		if (!bInProgress.compare_exchange_strong(bExpected, true))
		{
			return;
		}

		if (!GConnector)
		{
			GConnector = MakeUnique<FPencilConnector>();
		}

		const auto BeginFetch = []()
		{
			ShowNotification(LOCTEXT("Connecting", "Design2UMG: Fetching design from Pencil..."), SNotificationItem::CS_Pending);

			TSharedPtr<FJsonObject> BatchArgs = MakeShared<FJsonObject>();
			BatchArgs->SetStringField(TEXT("filePath"), TEXT(""));

			TArray<TSharedPtr<FJsonValue>> Patterns;
			TSharedPtr<FJsonObject> Pattern = MakeShared<FJsonObject>();
			Pattern->SetStringField(TEXT("type"), TEXT("frame"));
			Patterns.Add(MakeShared<FJsonValueObject>(Pattern));
			BatchArgs->SetArrayField(TEXT("patterns"), Patterns);
			BatchArgs->SetBoolField(TEXT("includePathGeometry"), true);
			BatchArgs->SetNumberField(TEXT("searchDepth"), 1);
			BatchArgs->SetNumberField(TEXT("readDepth"), 99);

			GConnector->GetClient().CallToolAsync(
				TEXT("batch_get"),
				BatchArgs,
				[](const bool bBatchOk, const FString& NodesJson)
				{
					if (!bBatchOk)
					{
						ShowNotification(LOCTEXT("BatchGetFailed", "Design2UMG: batch_get failed"), SNotificationItem::CS_Fail);
						bInProgress = false;
						return;
					}

					if (!GConnector)
					{
						bInProgress = false;
						return;
					}

					TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
					VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));

					GConnector->GetClient().CallToolAsync(
						TEXT("get_variables"),
						VarsArgs,
						[CapturedNodes = NodesJson](const bool bVarsOk, const FString& VarsJson)
						{
							const FString FinalVars = bVarsOk ? VarsJson : TEXT("{}");
							if (!bVarsOk)
							{
								UE_LOG(LogDesign2UMG, Warning, TEXT("ConnectToPencil: get_variables failed, using empty"));
							}

							OnMCPDataReady(CapturedNodes, FinalVars);
							bInProgress = false;
						});
				});
		};

		if (!GConnector->IsRunning())
		{
			ShowNotification(LOCTEXT("LaunchingMCP", "Design2UMG: Launching MCP Server..."), SNotificationItem::CS_Pending);
			GConnector->LaunchAsync(
				0,
				[BeginFetch](const bool bLaunchOk)
				{
					if (!bLaunchOk)
					{
						ShowNotification(LOCTEXT("LaunchFailed", "Design2UMG: Failed to launch MCP Server"), SNotificationItem::CS_Fail);
						bInProgress = false;
						return;
					}

					BeginFetch();
				});
			return;
		}

		BeginFetch();
	}

	void ImportPenFile(const FString& FilePath, const FString& OutputPath, const bool bUsePreview = true, const bool bRenderPng = true)
	{
		TArray<FDesignFrameInfo> Frames;
		if (!DesignImportPipeline::LoadPenFrames(FilePath, Frames))
		{
			return;
		}

		if (Frames.Num() == 0)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: No frames found in %s"), *FilePath);
			return;
		}

		ExecuteFrameImport(
			Frames,
			LOCTEXT("FrameSelectorTitle", "Select Frame to Import"),
			LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
			[CapturedFilePath = FilePath, CapturedOutputPath = OutputPath, bCapturedPreview = bUsePreview, bCapturedRenderPng = bRenderPng](const FString& FrameId)
			{
				DesignImportPipeline::ExecutePenFile(
					CapturedFilePath,
					FrameId,
					MakePenImportRequest(CapturedOutputPath, bCapturedPreview, bCapturedRenderPng));
			});
	}

	void ImportFromFigma(const FString& FileUrl, const FString& Token)
	{
		FString FileKey;
		if (!FFigmaParser::ExtractFileKey(FileUrl, FileKey))
		{
			ShowNotification(LOCTEXT("FigmaInvalidUrl", "Design2UMG: Invalid Figma URL or file key"), SNotificationItem::CS_Fail);
			RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma url validation failed"));
			return;
		}

		if (!GFigmaClient)
		{
			GFigmaClient = MakeUnique<FFigmaHttpClient>();
		}
		GFigmaClient->SetToken(Token);

		ShowNotification(LOCTEXT("FigmaFetching", "Design2UMG: Fetching design from Figma..."), SNotificationItem::CS_Pending);

		GFigmaClient->GetFile(
			FileKey,
			[FileKey](const bool bSuccess, const FString& Body)
			{
				if (!bSuccess)
				{
					ShowNotification(LOCTEXT("FigmaGetFileFailed", "Design2UMG: Figma API request failed"), SNotificationItem::CS_Fail);
					RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma api request failed"));
					return;
				}

				FDesignDocument ParsedDoc;
				if (!FFigmaParser::ParseDocument(Body, ParsedDoc))
				{
					ShowNotification(LOCTEXT("FigmaParseFailed", "Design2UMG: Failed to parse Figma file"), SNotificationItem::CS_Fail);
					RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma parse failed"));
					return;
				}

				TSharedRef<FDesignDocument> Doc = MakeShared<FDesignDocument>(MoveTemp(ParsedDoc));
				if (!GFigmaClient)
				{
					return;
				}

				FFigmaImageExporter::ResolveImageRefs(
					*GFigmaClient,
					FileKey,
					Doc,
					[Doc, FileKey](const bool bImgOk)
					{
						if (!bImgOk)
						{
							UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some image refs unresolved"));
						}

						if (!GFigmaClient)
						{
							return;
						}

						FFigmaImageExporter::ExportVectorNodes(
							*GFigmaClient,
							FileKey,
							Doc,
							[Doc](const bool bVecOk, const TMap<FString, FString>& IdToUrl)
							{
								if (!bVecOk)
								{
									UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some vector nodes not exported"));
								}

								if (IdToUrl.Num() > 0)
								{
									FFigmaImageExporter::ApplyExportedImages(Doc->Children, IdToUrl);
								}

								FUMGWidgetData Data;
								DesignImportPipeline::ConvertDocument(*Doc, Data);
								if (Data.RootWidget.Children.Num() == 0)
								{
									ShowNotification(LOCTEXT("FigmaNoFrames", "Design2UMG: No frames found in Figma file"), SNotificationItem::CS_Fail);
									RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma no frames"));
									return;
								}

								ShowNotification(
									FText::Format(
										LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
										FText::AsNumber(Data.RootWidget.Children.Num())),
									SNotificationItem::CS_Success);

								DesignImportPipeline::Execute(
									Data,
									MakeImportRequest(
										DefaultFigmaOutputPath,
										LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
										!FApp::IsUnattended(),
										EDesignBlueprintGenerationMode::BlueprintAndPng));

								RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma import"));
							});
					});
			});
	}

	void ImportLocalFigmaExport(const FString& JsonFilePath, const FString& OutputPath, const bool bUsePreview)
	{
		FDesignDocument Document;
		FString ErrorMessage;
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(JsonFilePath, Document, ErrorMessage))
		{
			ShowNotification(LOCTEXT("LocalFigmaImportFailed", "Design2UMG: Failed to import local Figma export"), SNotificationItem::CS_Fail);
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *ErrorMessage);
			return;
		}

		const FString FinalOutputPath = OutputPath.IsEmpty()
			? FString::Printf(TEXT("/Game/Design2UMG/Figma_%s"), *FDateTime::Now().ToString(TEXT("%m%d_%H%M%S")))
			: OutputPath;

		DesignImportPipeline::Execute(
			Document,
			MakeImportRequest(
				FinalOutputPath,
				LOCTEXT("LocalFigmaPreviewTitle", "Design2UMG Local Figma Preview"),
				bUsePreview && !FApp::IsUnattended(),
				EDesignBlueprintGenerationMode::BlueprintAndPng,
				false));
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
						return FReply::Handled();
					}

					if (const TSharedPtr<SWindow> Win = WeakWindow.Pin())
					{
						Win->RequestDestroyWindow();
					}

					ImportFromFigma(Url, Token);
					return FReply::Handled();
				})
			]);

		FSlateApplication::Get().AddWindow(Window);
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
		TSharedPtr<SEditableTextBox> WidthBox;
		TSharedPtr<SEditableTextBox> HeightBox;
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
				[ SNew(STextBlock).Text(LOCTEXT("DesignRes", "Design Resolution:")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SAssignNew(WidthBox, SEditableTextBox).Text(FText::AsNumber(Settings->DesignWidth)) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[ SNew(STextBlock).Text(LOCTEXT("ResX", "x")) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SAssignNew(HeightBox, SEditableTextBox).Text(FText::AsNumber(Settings->DesignHeight)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 4.0f)
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
				.OnClicked_Lambda([WeakWindow, Settings, WidthBox, HeightBox, OutputPathBox]()
				{
					Settings->DesignWidth = FCString::Atoi(*WidthBox->GetText().ToString());
					Settings->DesignHeight = FCString::Atoi(*HeightBox->GetText().ToString());
					Settings->RemoteImageImportPath = OutputPathBox->GetText().ToString();
					Settings->bFirstRunCompleted = true;
					Settings->SaveConfig();
					if (const TSharedPtr<SWindow> Win = WeakWindow.Pin())
					{
						Win->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]);

		FSlateApplication::Get().AddWindow(Window);
	}

	void OpenPenFileDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			return;
		}

		TArray<FString> OutFiles;
		const bool bOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import Pencil Design File"),
			FPaths::ProjectDir(),
			TEXT(""),
			TEXT("Pencil Design Files (*.pen)|*.pen"),
			EFileDialogFlags::None,
			OutFiles);

		if (bOpened && OutFiles.Num() > 0)
		{
			ImportPenFile(OutFiles[0], DefaultPenOutputPath);
		}
	}

	void OpenLocalFigmaJsonDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			return;
		}

		TArray<FString> OutFiles;
		const bool bOpened = Desktop->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import Local Figma Export (design.json)"),
			FPaths::ProjectDir(),
			TEXT("design.json"),
			TEXT("Figma Export JSON (design.json)|design.json|JSON Files (*.json)|*.json"),
			EFileDialogFlags::None,
			OutFiles);

		if (bOpened && OutFiles.Num() > 0)
		{
			ImportLocalFigmaExport(OutFiles[0], DefaultFigmaOutputPath, true);
		}
	}
}

namespace Design2UMGEditorActions
{
	void RegisterBuiltInDesignSources()
	{
		RegisterDesignSource(TEXT("Pen"), [](const FString& RawJson, FUMGWidgetData& OutData)
		{
			return DesignImportPipeline::ImportPenJson(RawJson, OutData);
		});

		RegisterDesignSource(TEXT("Figma"), [](const FString& RawJson, FUMGWidgetData& OutData)
		{
			FDesignDocument Doc;
			if (!FFigmaParser::ParseDocument(RawJson, Doc))
			{
				return false;
			}

			DesignImportPipeline::ConvertDocument(Doc, OutData);
			return true;
		});
	}

	void UnregisterBuiltInDesignSources()
	{
		UnregisterDesignSource(TEXT("Pen"));
		UnregisterDesignSource(TEXT("Figma"));
	}

	void RegisterConsoleCommands()
	{
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.PenConvert"),
			TEXT("Convert from MCP JSON: Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				FPenConvertCommandArgs CmdArgs;
				if (!ParsePenConvertCommandArgs(Args, CmdArgs))
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"));
					return;
				}

				FString NodesStr;
				FString VarsStr;
				if (!LoadTextFileOrLog(CmdArgs.NodesPath, NodesStr)
					|| !LoadTextFileOrLog(CmdArgs.VarsPath, VarsStr))
				{
					return;
				}

				FUMGWidgetData Data;
				if (!FPenMCPBridge::ConvertFromMCP(NodesStr, VarsStr, Data, CmdArgs.TargetNodeId))
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] PenConvert failed"));
					return;
				}

				UE_LOG(LogDesign2UMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
				for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
				{
					const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
					UE_LOG(LogDesign2UMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
				}

				DesignImportPipeline::Execute(Data, MakePenImportRequest(CmdArgs.OutputPath, CmdArgs.bUsePreview, true));

				RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after pen convert"));
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.Import"),
			TEXT("Import .pen file: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]"));
					return;
				}

				const FString FilePath = Args[0];
				FImportCommandOptions Options;
				ParsePenImportCommandArgs(Args, 1, Options);

				if (!FPaths::FileExists(FilePath))
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *FilePath);
					return;
				}

				if (Options.TargetNodeId.IsEmpty())
				{
					ImportPenFile(FilePath, Options.OutputPath, Options.bUsePreview, Options.bRenderPng);
				}
				else
				{
					DesignImportPipeline::ExecutePenFile(
						FilePath,
						Options.TargetNodeId,
						MakePenImportRequest(Options.OutputPath, Options.bUsePreview, Options.bRenderPng));
				}

				RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after import"));
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.ImportSource"),
			TEXT("Import from registered source: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					TArray<FName> Sources;
					GetDesignSourceRegistry().GetKeys(Sources);
					UE_LOG(LogDesign2UMG, Log, TEXT("Usage: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
					UE_LOG(LogDesign2UMG, Log, TEXT("Registered sources (%d):"), Sources.Num());
					for (const FName& Source : Sources)
					{
						UE_LOG(LogDesign2UMG, Log, TEXT("  - %s"), *Source.ToString());
					}
					return;
				}

				if (Args.Num() < 2)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
					return;
				}

				const FName SourceId(*Args[0]);
				const FString JsonPath = Args[1];
				FString OutputPath = DefaultPenOutputPath;
				bool bPreview = true;

				for (int32 i = 2; i < Args.Num(); ++i)
				{
					ApplyOutputPreviewArg(Args[i], OutputPath, bPreview);
				}

				if (!GetDesignSourceRegistry().Contains(SourceId))
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("Unknown source: %s"), *SourceId.ToString());
					return;
				}

				FString JsonStr;
				if (!LoadTextFileOrLog(JsonPath, JsonStr))
				{
					return;
				}

				FUMGWidgetData Data;
				FDesignSourceConvertFunc* ConvertFunc = GetDesignSourceRegistry().Find(SourceId);
				if (!ConvertFunc || !(*ConvertFunc)(JsonStr, Data))
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] ImportSource convert failed for: %s"), *SourceId.ToString());
					return;
				}

				UE_LOG(LogDesign2UMG, Log, TEXT("[OK] ImportSource succeeded (%s), root children: %d"), *SourceId.ToString(), Data.RootWidget.Children.Num());

				DesignImportPipeline::Execute(
					Data,
					MakeImportRequest(
						OutputPath,
						LOCTEXT("ImportSourcePreviewTitle", "Design2UMG Import Preview"),
						bPreview,
						EDesignBlueprintGenerationMode::BlueprintAndPng));

				RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after import source"));
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.FigmaImport"),
			TEXT("Import from Figma: Design2UMG.FigmaImport <file_url_or_key> <token_from_secure_source>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.FigmaImport <file_url_or_key> <token_from_secure_source>"));
					return;
				}

				ImportFromFigma(Args[0], Args[1]);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.LocalFigmaImport"),
			TEXT("Import local Figma export: Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]"));
					return;
				}

				const FString JsonPath = Args[0];
				FString OutputPath;
				bool bPreview = true;
				for (int32 i = 1; i < Args.Num(); ++i)
				{
					ApplyOutputPreviewArg(Args[i], OutputPath, bPreview);
				}

				if (!FPaths::FileExists(JsonPath))
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *JsonPath);
					return;
				}

				ImportLocalFigmaExport(JsonPath, OutputPath, bPreview);
				RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after local figma import"));
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.AutomationRender"),
			TEXT("Automation render from manifest JSON: Design2UMG.AutomationRender <spec.json>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				Design2UMGAutomationRender::HandleConsoleCommand(Args);
			}),
			ECVF_Default);
	}

	void UnregisterConsoleCommands()
	{
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.Import"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.PenConvert"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.ImportSource"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.FigmaImport"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.LocalFigmaImport"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.AutomationRender"), false);
	}

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
				FUIAction(FExecuteAction::CreateLambda([]() { OpenPenFileDialog(); })));

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

		UDesign2UMGSettings* WelcomeSettings = UDesign2UMGSettings::Get();
		if (!WelcomeSettings || WelcomeSettings->bFirstRunCompleted || !FSlateApplication::IsInitialized())
		{
			return;
		}

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WelcomeSettings](float) -> bool
			{
				if (!WelcomeSettings->bFirstRunCompleted)
				{
					ShowWelcomeDialog(WelcomeSettings);
				}
				return false;
			}),
			1.0f);
	}

	void ShutdownTransientState()
	{
		if (GConnector)
		{
			GConnector->Shutdown();
			GConnector.Reset();
		}

		GFigmaClient.Reset();

		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
	}
}

#undef LOCTEXT_NAMESPACE

