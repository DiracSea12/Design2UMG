// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"
#include "Design2UMGLog.h"
#include "PenMCPBridge.h"
#include "PenImportUtils.h"

#include "PenParser.h"
#include "PenImporter.h"
#include "DesignSourceRegistry.h"
#include "PencilConnector.h"

#include "FigmaParser.h"
#include "FigmaHttpClient.h"
#include "FigmaImageExporter.h"
#include "Design2UMGConverter.h"

#include "WidgetBlueprint.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY(LogDesign2UMG);

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	const TCHAR* DefaultPenOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	/** 模块级 Pencil MCP 连接器 */
	TUniquePtr<FPencilConnector> GConnector;

	struct FPenConvertCommandArgs
	{
		FString NodesPath;
		FString VarsPath;
		FString TargetNodeId;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
	};

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
			if (Arg == TEXT("--preview"))
			{
				Out.bUsePreview = true;
				continue;
			}
			if (Arg == TEXT("--no-preview"))
			{
				Out.bUsePreview = false;
				continue;
			}
			if (Arg.StartsWith(TEXT("--out=")))
			{
				Out.OutputPath = Arg.RightChop(6);
				continue;
			}
			if (Out.TargetNodeId.IsEmpty())
			{
				Out.TargetNodeId = Arg;
			}
		}
		return true;
	}

	/** 从 MCP 数据执行转换 + 预览/生成 */
	void ProcessMCPData(const FString& NodesJson, const FString& VarsJson, const FString& TargetNodeId, const FString& OutputPath)
	{
		FUMGWidgetData Data;
		if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, Data, TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: ConvertFromMCP failed"));
			return;
		}

		PenImportUtils::ResolveImages(Data);

		if (PenImportUtils::OpenPreviewWindow(Data, OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview")))
			return;

		PenImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
	}

	/** 从 batch_get 结果中提取 frame 信息列表 */
	TArray<FPenFrameInfo> ExtractFramesFromMCPResult(const FString& NodesJson)
	{
		TArray<FPenFrameInfo> Result;

		TArray<TSharedPtr<FJsonValue>> Arr;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
		if (!FJsonSerializer::Deserialize(Reader, Arr))
			return Result;

		for (const auto& Val : Arr)
		{
			const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
			if (!Obj.IsValid())
				continue;

			FPenFrameInfo Info;
			Obj->TryGetStringField(TEXT("id"), Info.Id);
			Obj->TryGetStringField(TEXT("name"), Info.Name);
			if (Info.Name.IsEmpty())
				Info.Name = Info.Id;

			Info.Width = 0.0f;
			Info.Height = 0.0f;
			double W = 0, H = 0;
			Obj->TryGetNumberField(TEXT("width"), W);
			Obj->TryGetNumberField(TEXT("height"), H);
			Info.Width = static_cast<float>(W);
			Info.Height = static_cast<float>(H);

			Result.Add(MoveTemp(Info));
		}
		return Result;
	}

	/** 显示通知 */
	void ShowNotification(const FText& Text, SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			Item->SetCompletionState(State);
	}

	/** 异步导入完成后的处理 */
	void OnMCPDataReady(const FString& NodesJson, const FString& VarsJson)
	{
		TArray<FPenFrameInfo> Frames = ExtractFramesFromMCPResult(NodesJson);
		if (Frames.Num() == 0)
		{
			ShowNotification(LOCTEXT("NoFrames", "Design2UMG: No frames found in Pencil document"), SNotificationItem::CS_Fail);
			return;
		}

		const FString OutputPath = DefaultPenOutputPath;

		if (Frames.Num() == 1)
		{
			ProcessMCPData(NodesJson, VarsJson, TEXT(""), OutputPath);
			return;
		}

		FString CapturedNodes = NodesJson;
		FString CapturedVars = VarsJson;
		FString CapturedOutput = OutputPath;

		PenImportUtils::ShowFrameSelector(
			Frames,
			LOCTEXT("PencilFrameSelectorTitle", "Select Frame from Pencil"),
			LOCTEXT("PencilFrameSelectorHint", "Multiple frames found. Double-click to import:"),
			[CapturedNodes, CapturedVars, CapturedOutput](const FString& FrameId)
			{
				ProcessMCPData(CapturedNodes, CapturedVars, FrameId, CapturedOutput);
			});
	}

	/** Connect to Pencil 主流程（异步） */
	void ConnectToPencilAndImport()
	{
		static bool bInProgress = false;
		if (bInProgress)
			return;
		bInProgress = true;

		// 1. 启动 / 复用连接
		if (!GConnector)
			GConnector = MakeUnique<FPencilConnector>();

		if (!GConnector->IsRunning())
		{
			if (!GConnector->Launch())
			{
				ShowNotification(LOCTEXT("LaunchFailed", "Design2UMG: Failed to launch MCP Server"), SNotificationItem::CS_Fail);
				bInProgress = false;
				return;
			}
		}

		ShowNotification(LOCTEXT("Connecting", "Design2UMG: Fetching design from Pencil..."), SNotificationItem::CS_Pending);

		// 2. 异步 batch_get
		TSharedPtr<FJsonObject> BatchArgs = MakeShared<FJsonObject>();
		BatchArgs->SetStringField(TEXT("filePath"), TEXT(""));

		TArray<TSharedPtr<FJsonValue>> Patterns;
		TSharedPtr<FJsonObject> Pattern = MakeShared<FJsonObject>();
		Pattern->SetStringField(TEXT("type"), TEXT("frame"));
		Patterns.Add(MakeShared<FJsonValueObject>(Pattern));
		BatchArgs->SetArrayField(TEXT("patterns"), Patterns);
		BatchArgs->SetNumberField(TEXT("searchDepth"), 1);
		BatchArgs->SetNumberField(TEXT("readDepth"), 99);

		GConnector->GetClient().CallToolAsync(TEXT("batch_get"), BatchArgs,
			[](bool bBatchOk, const FString& NodesJson)
		{
			if (!bBatchOk)
			{
				ShowNotification(LOCTEXT("BatchGetFailed", "Design2UMG: batch_get failed"), SNotificationItem::CS_Fail);
				bInProgress = false;
				return;
			}

			// 3. 异步 get_variables
			if (!GConnector)
			{
				bInProgress = false;
				return;
			}

			TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
			VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));

			FString CapturedNodes = NodesJson;

			GConnector->GetClient().CallToolAsync(TEXT("get_variables"), VarsArgs,
				[CapturedNodes](bool bVarsOk, const FString& VarsJson)
			{
				FString FinalVars = bVarsOk ? VarsJson : TEXT("{}");
				if (!bVarsOk)
					UE_LOG(LogDesign2UMG, Warning, TEXT("ConnectToPencil: get_variables failed, using empty"));

				OnMCPDataReady(CapturedNodes, FinalVars);
				bInProgress = false;
			});
		});
	}

	/** 导入指定 TargetNodeId 的 frame */
	void ImportPenFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath, bool bUsePreview)
	{
		FUMGWidgetData Data;
		if (!FPenImporter::ImportFileWithTargetNode(FilePath, TargetNodeId, Data))
			return;

		PenImportUtils::ResolveImages(Data);

		if (bUsePreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview")))
			return;

		PenImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
	}

	/** 导入 .pen 文件（工具栏 + 控制台命令共用） */
	void ImportPenFile(const FString& FilePath, const FString& OutputPath, bool bUsePreview = true)
	{
		// 先解析文档，检查顶层 frame 数量
		FPenDocument Document;
		if (!FPenParser::ParseFromFile(FilePath, Document))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: Failed to parse %s"), *FilePath);
			return;
		}

		TArray<FPenFrameInfo> Frames = ExtractTopFrames(Document);
		if (Frames.Num() == 0)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: No frames found in %s"), *FilePath);
			return;
		}

		if (Frames.Num() == 1)
		{
			// 单 frame 快速路径
			FUMGWidgetData Data;
			FString JsonStr;
			if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: Failed to read file: %s"), *FilePath);
				return;
			}
			if (!FDesignSourceRegistry::Convert(TEXT("Pen"), JsonStr, Data))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: Convert failed: %s"), *FilePath);
				return;
			}

			PenImportUtils::ResolveImages(Data);

			if (bUsePreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
				LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview")))
				return;

			PenImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
			return;
		}

		// 多 frame → 弹出选择器
		FString CapturedFilePath = FilePath;
		FString CapturedOutputPath = OutputPath;
		bool bCapturedPreview = bUsePreview;

		PenImportUtils::ShowFrameSelector(
			Frames,
			LOCTEXT("FrameSelectorTitle", "Select Frame to Import"),
			LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
			[CapturedFilePath, CapturedOutputPath, bCapturedPreview](const FString& FrameId)
			{
				ImportPenFileWithTargetNode(CapturedFilePath, FrameId, CapturedOutputPath, bCapturedPreview);
			});
	}

	// ── Figma 导入 ──

	const TCHAR* DefaultFigmaOutputPath = TEXT("/Game/Design2UMG/FigmaConvert_Output");

	/** 模块级 Figma HTTP 客户端 */
	TUniquePtr<FFigmaHttpClient> GFigmaClient;

	/** Figma 导入主流程（异步） */
	void ImportFromFigma(const FString& FileUrl, const FString& Token)
	{
		FString FileKey;
		if (!FFigmaParser::ExtractFileKey(FileUrl, FileKey))
		{
			ShowNotification(LOCTEXT("FigmaInvalidUrl", "Design2UMG: Invalid Figma URL or file key"), SNotificationItem::CS_Fail);
			return;
		}

		if (!GFigmaClient)
			GFigmaClient = MakeUnique<FFigmaHttpClient>();
		GFigmaClient->SetToken(Token);

		ShowNotification(LOCTEXT("FigmaFetching", "Design2UMG: Fetching design from Figma..."), SNotificationItem::CS_Pending);

		GFigmaClient->GetFile(FileKey,
			[FileKey](bool bSuccess, const FString& Body)
			{
				if (!bSuccess)
				{
					ShowNotification(LOCTEXT("FigmaGetFileFailed", "Design2UMG: Figma API request failed"), SNotificationItem::CS_Fail);
					return;
				}

				FPenDocument ParsedDoc;
				if (!FFigmaParser::ParseDocument(Body, ParsedDoc))
				{
					ShowNotification(LOCTEXT("FigmaParseFailed", "Design2UMG: Failed to parse Figma file"), SNotificationItem::CS_Fail);
					return;
				}

				// shared ownership for async safety
				TSharedRef<FPenDocument> Doc = MakeShared<FPenDocument>(MoveTemp(ParsedDoc));

				// 解析图片引用
				if (GFigmaClient)
				{
					FFigmaImageExporter::ResolveImageRefs(*GFigmaClient, FileKey, Doc,
						[Doc, FileKey](bool bImgOk)
						{
							if (!bImgOk)
								UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some image refs unresolved"));

							// 将向量节点导出为 PNG
							if (GFigmaClient)
							{
								FFigmaImageExporter::ExportVectorNodes(*GFigmaClient, FileKey, Doc,
									[Doc](bool bVecOk)
									{
										if (!bVecOk)
											UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some vector nodes not exported"));

										FUMGWidgetData Data = FDesign2UMGConverter::Convert(*Doc);
										PenImportUtils::ResolveImages(Data);

										if (Data.RootWidget.Children.Num() == 0)
										{
											ShowNotification(LOCTEXT("FigmaNoFrames", "Design2UMG: No frames found in Figma file"), SNotificationItem::CS_Fail);
											return;
										}

										ShowNotification(
											FText::Format(LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
												FText::AsNumber(Data.RootWidget.Children.Num())),
											SNotificationItem::CS_Success);

										PenImportUtils::OpenPreviewWindow(Data, DefaultFigmaOutputPath,
											LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"));
									});
							}
						});
				}
			});
	}

	/** 弹出 Figma 导入对话框 */
	void ShowFigmaImportDialog()
	{
		if (!FSlateApplication::IsInitialized())
			return;

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
					FString Url = UrlBox->GetText().ToString().TrimStartAndEnd();
					FString Token = TokenBox->GetText().ToString().TrimStartAndEnd();
					if (Url.IsEmpty() || Token.IsEmpty())
						return FReply::Handled();

					if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						Win->RequestDestroyWindow();

					ImportFromFigma(Url, Token);
					return FReply::Handled();
				})
			]
		);

		FSlateApplication::Get().AddWindow(Window);
	}

	/** 打开文件对话框选择 .pen 文件并导入 */
	void OpenPenFileDialog()
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
			return;

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
}

void FDesign2UMGEditorModule::StartupModule()
{
	// 注册 Pen 数据源适配器
	FDesignSourceRegistry::Register(TEXT("Pen"), [](const FString& RawJson, FUMGWidgetData& OutData){
		return FPenImporter::ImportFromJson(RawJson, OutData);
	});

	// 注册 Figma 数据源适配器
	FDesignSourceRegistry::Register(TEXT("Figma"), [](const FString& RawJson, FUMGWidgetData& OutData){
		FPenDocument Doc;
		if (!FFigmaParser::ParseDocument(RawJson, Doc))
			return false;
		OutData = FDesign2UMGConverter::Convert(Doc);
		return true;
	});

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
			if (!FFileHelper::LoadFileToString(NodesStr, *CmdArgs.NodesPath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *CmdArgs.NodesPath);
				return;
			}
			if (!FFileHelper::LoadFileToString(VarsStr, *CmdArgs.VarsPath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *CmdArgs.VarsPath);
				return;
			}

			FUMGWidgetData Data;
			if (!FPenMCPBridge::ConvertFromMCP(NodesStr, VarsStr, Data, CmdArgs.TargetNodeId))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] PenConvert failed"));
				return;
			}

			PenImportUtils::ResolveImages(Data);

			UE_LOG(LogDesign2UMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
			for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
			{
				const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
				UE_LOG(LogDesign2UMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
			}

			if (CmdArgs.bUsePreview && PenImportUtils::OpenPreviewWindow(Data, CmdArgs.OutputPath,
				LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview")))
			{
				return;
			}

			PenImportUtils::GenerateBlueprintAndPng(Data, CmdArgs.OutputPath);
		}),
		ECVF_Default);

	// Design2UMG.Import 命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Design2UMG.Import"),
		TEXT("Import .pen file: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview]"));
				return;
			}

			const FString FilePath = Args[0];
			FString OutputPath = DefaultPenOutputPath;
			FString TargetNodeId;
			bool bPreview = true;

			for (int32 i = 1; i < Args.Num(); ++i)
			{
				if (Args[i].StartsWith(TEXT("--out=")))
					OutputPath = Args[i].RightChop(6);
				else if (Args[i].StartsWith(TEXT("--node=")))
					TargetNodeId = Args[i].RightChop(7);
				else if (Args[i] == TEXT("--no-preview"))
					bPreview = false;
			}

			if (!FPaths::FileExists(FilePath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *FilePath);
				return;
			}

			if (TargetNodeId.IsEmpty())
			{
				ImportPenFile(FilePath, OutputPath, bPreview);
			}
			else
			{
				ImportPenFileWithTargetNode(FilePath, TargetNodeId, OutputPath, bPreview);
			}

			// Unattended 模式下自动退出，避免编辑器空转渲染导致 GPU PageFault
			if (FApp::IsUnattended())
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("Unattended mode: requesting engine exit after import"));
				FPlatformMisc::RequestExit(false);
			}
		}),
		ECVF_Default);

	// Design2UMG.ImportSource 通用数据源导入命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Design2UMG.ImportSource"),
		TEXT("Import from registered source: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				TArray<FName> Sources = FDesignSourceRegistry::GetRegisteredSources();
				UE_LOG(LogDesign2UMG, Log, TEXT("Usage: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
				UE_LOG(LogDesign2UMG, Log, TEXT("Registered sources (%d):"), Sources.Num());
				for (const FName& S : Sources)
				{
					UE_LOG(LogDesign2UMG, Log, TEXT("  - %s"), *S.ToString());
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
				if (Args[i].StartsWith(TEXT("--out=")))
					OutputPath = Args[i].RightChop(6);
				else if (Args[i] == TEXT("--no-preview"))
					bPreview = false;
			}

			if (!FDesignSourceRegistry::IsRegistered(SourceId))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Unknown source: %s"), *SourceId.ToString());
				return;
			}

			FString JsonStr;
			if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *JsonPath);
				return;
			}

			FUMGWidgetData Data;
			if (!FDesignSourceRegistry::Convert(SourceId, JsonStr, Data))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] ImportSource convert failed for: %s"), *SourceId.ToString());
				return;
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("[OK] ImportSource succeeded (%s), root children: %d"), *SourceId.ToString(), Data.RootWidget.Children.Num());

			if (bPreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
				LOCTEXT("ImportSourcePreviewTitle", "Design2UMG Import Preview")))
			{
				return;
			}

			PenImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
		}),
		ECVF_Default);

	// Design2UMG.FigmaImport 控制台命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Design2UMG.FigmaImport"),
		TEXT("Import from Figma: Design2UMG.FigmaImport <file_url_or_key> <token>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.FigmaImport <file_url_or_key> <token>"));
				return;
			}
			ImportFromFigma(Args[0], Args[1]);
		}),
		ECVF_Default);

	// 工具栏菜单注册
	ToolMenusStartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
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
	}));

}

void FDesign2UMGEditorModule::ShutdownModule()
{
	// 关闭 Pencil MCP 连接
	if (GConnector)
	{
		GConnector->Shutdown();
		GConnector.Reset();
	}

	// 清理 Figma 客户端
	GFigmaClient.Reset();

	UToolMenus::UnRegisterStartupCallback(ToolMenusStartupHandle);
	UToolMenus::UnregisterOwner(this);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.Import"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.PenConvert"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.ImportSource"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.FigmaImport"), false);

	// 反注册数据源适配器
	FDesignSourceRegistry::Unregister(TEXT("Pen"));
	FDesignSourceRegistry::Unregister(TEXT("Figma"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
