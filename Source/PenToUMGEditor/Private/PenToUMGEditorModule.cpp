// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGEditorModule.h"
#include "PenToUMGLog.h"
#include "PenMCPBridge.h"
#include "PenImportUtils.h"

#include "PenParser.h"
#include "PenImporter.h"
#include "DesignSourceRegistry.h"
#include "PencilConnector.h"

#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

DEFINE_LOG_CATEGORY(LogPenToUMG);

#define LOCTEXT_NAMESPACE "FPenToUMGEditorModule"

namespace
{
	const TCHAR* DefaultPenOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

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
			UE_LOG(LogPenToUMG, Error, TEXT("ConnectToPencil: ConvertFromMCP failed"));
			return;
		}

		PenImportUtils::ResolveImages(Data);

		if (PenImportUtils::OpenPreviewWindow(Data, OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "PenToUMG Pen Preview")))
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

	/** Connect to Pencil 主流程 */
	void ConnectToPencilAndImport()
	{
		// 防止 Slate Tick 期间用户重复触发
		static bool bInProgress = false;
		if (bInProgress)
			return;
		TGuardValue<bool> Guard(bInProgress, true);

		// 1. 启动 / 复用连接
		if (!GConnector)
			GConnector = MakeUnique<FPencilConnector>();

		if (!GConnector->IsRunning())
		{
			if (!GConnector->Launch())
			{
				UE_LOG(LogPenToUMG, Error, TEXT("ConnectToPencil: Failed to launch MCP Server"));
				return;
			}
		}

		FMCPHttpClient& Client = GConnector->GetClient();

		// 2. batch_get: 获取顶层 frame 列表
		TSharedPtr<FJsonObject> BatchArgs = MakeShared<FJsonObject>();
		BatchArgs->SetStringField(TEXT("filePath"), TEXT(""));

		TArray<TSharedPtr<FJsonValue>> Patterns;
		TSharedPtr<FJsonObject> Pattern = MakeShared<FJsonObject>();
		Pattern->SetStringField(TEXT("type"), TEXT("frame"));
		Patterns.Add(MakeShared<FJsonValueObject>(Pattern));
		BatchArgs->SetArrayField(TEXT("patterns"), Patterns);
		BatchArgs->SetNumberField(TEXT("searchDepth"), 1);
		BatchArgs->SetNumberField(TEXT("readDepth"), 99);

		FString NodesJson;
		if (!Client.CallTool(TEXT("batch_get"), BatchArgs, NodesJson))
		{
			UE_LOG(LogPenToUMG, Error, TEXT("ConnectToPencil: batch_get failed"));
			return;
		}

		// 3. get_variables: 获取变量/主题
		TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
		VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));

		FString VarsJson;
		if (!Client.CallTool(TEXT("get_variables"), VarsArgs, VarsJson))
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("ConnectToPencil: get_variables failed, using empty"));
			VarsJson = TEXT("{}");
		}

		// 4. 解析 frame 列表
		TArray<FPenFrameInfo> Frames = ExtractFramesFromMCPResult(NodesJson);
		if (Frames.Num() == 0)
		{
			UE_LOG(LogPenToUMG, Error, TEXT("ConnectToPencil: No frames found in Pencil document"));
			return;
		}

		const FString OutputPath = DefaultPenOutputPath;

		if (Frames.Num() == 1)
		{
			ProcessMCPData(NodesJson, VarsJson, TEXT(""), OutputPath);
			return;
		}

		// 5. 多 frame → 弹出选择器
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

	/** 导入指定 TargetNodeId 的 frame */
	void ImportPenFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath, bool bUsePreview)
	{
		FUMGWidgetData Data;
		if (!FPenImporter::ImportFileWithTargetNode(FilePath, TargetNodeId, Data))
			return;

		PenImportUtils::ResolveImages(Data);

		if (bUsePreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "PenToUMG Pen Preview")))
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
			UE_LOG(LogPenToUMG, Error, TEXT("PenToUMG.Import: Failed to parse %s"), *FilePath);
			return;
		}

		TArray<FPenFrameInfo> Frames = ExtractTopFrames(Document);
		if (Frames.Num() == 0)
		{
			UE_LOG(LogPenToUMG, Error, TEXT("PenToUMG.Import: No frames found in %s"), *FilePath);
			return;
		}

		if (Frames.Num() == 1)
		{
			// 单 frame 快速路径
			FUMGWidgetData Data;
			FString JsonStr;
			if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("PenToUMG.Import: Failed to read file: %s"), *FilePath);
				return;
			}
			if (!FDesignSourceRegistry::Convert(TEXT("Pen"), JsonStr, Data))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("PenToUMG.Import: Convert failed: %s"), *FilePath);
				return;
			}

			PenImportUtils::ResolveImages(Data);

			if (bUsePreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
				LOCTEXT("PenConvertPreviewWindowTitle", "PenToUMG Pen Preview")))
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

void FPenToUMGEditorModule::StartupModule()
{
	// 注册 Pen 数据源适配器
	FDesignSourceRegistry::Register(TEXT("Pen"), [](const FString& RawJson, FUMGWidgetData& OutData){
		return FPenImporter::ImportFromJson(RawJson, OutData);
	});

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PenToUMG.PenConvert"),
		TEXT("Convert from MCP JSON: PenToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FPenConvertCommandArgs CmdArgs;
			if (!ParsePenConvertCommandArgs(Args, CmdArgs))
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("Usage: PenToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"));
				return;
			}

			FString NodesStr;
			FString VarsStr;
			if (!FFileHelper::LoadFileToString(NodesStr, *CmdArgs.NodesPath))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("Failed to read file: %s"), *CmdArgs.NodesPath);
				return;
			}
			if (!FFileHelper::LoadFileToString(VarsStr, *CmdArgs.VarsPath))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("Failed to read file: %s"), *CmdArgs.VarsPath);
				return;
			}

			FUMGWidgetData Data;
			if (!FPenMCPBridge::ConvertFromMCP(NodesStr, VarsStr, Data, CmdArgs.TargetNodeId))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("[FAIL] PenConvert failed"));
				return;
			}

			PenImportUtils::ResolveImages(Data);

			UE_LOG(LogPenToUMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
			for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
			{
				const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
				UE_LOG(LogPenToUMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
			}

			if (CmdArgs.bUsePreview && PenImportUtils::OpenPreviewWindow(Data, CmdArgs.OutputPath,
				LOCTEXT("PenConvertPreviewWindowTitle", "PenToUMG Pen Preview")))
			{
				return;
			}

			PenImportUtils::GenerateBlueprintAndPng(Data, CmdArgs.OutputPath);
		}),
		ECVF_Default);

	// PenToUMG.Import 命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PenToUMG.Import"),
		TEXT("Import .pen file: PenToUMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("Usage: PenToUMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview]"));
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
				UE_LOG(LogPenToUMG, Error, TEXT("File not found: %s"), *FilePath);
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
		}),
		ECVF_Default);

	// PenToUMG.ImportSource 通用数据源导入命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PenToUMG.ImportSource"),
		TEXT("Import from registered source: PenToUMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				TArray<FName> Sources = FDesignSourceRegistry::GetRegisteredSources();
				UE_LOG(LogPenToUMG, Log, TEXT("Usage: PenToUMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
				UE_LOG(LogPenToUMG, Log, TEXT("Registered sources (%d):"), Sources.Num());
				for (const FName& S : Sources)
				{
					UE_LOG(LogPenToUMG, Log, TEXT("  - %s"), *S.ToString());
				}
				return;
			}

			if (Args.Num() < 2)
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("Usage: PenToUMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
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
				UE_LOG(LogPenToUMG, Error, TEXT("Unknown source: %s"), *SourceId.ToString());
				return;
			}

			FString JsonStr;
			if (!FFileHelper::LoadFileToString(JsonStr, *JsonPath))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("Failed to read file: %s"), *JsonPath);
				return;
			}

			FUMGWidgetData Data;
			if (!FDesignSourceRegistry::Convert(SourceId, JsonStr, Data))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("[FAIL] ImportSource convert failed for: %s"), *SourceId.ToString());
				return;
			}

			UE_LOG(LogPenToUMG, Log, TEXT("[OK] ImportSource succeeded (%s), root children: %d"), *SourceId.ToString(), Data.RootWidget.Children.Num());

			if (bPreview && PenImportUtils::OpenPreviewWindow(Data, OutputPath,
				LOCTEXT("ImportSourcePreviewTitle", "PenToUMG Import Preview")))
			{
				return;
			}

			PenImportUtils::GenerateBlueprintAndPng(Data, OutputPath);
		}),
		ECVF_Default);

	// 工具栏菜单注册
	ToolMenusStartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu"));
		UToolMenu* SubMenu = Menu->AddSubMenu(
			TEXT("PenToUMG"),
			NAME_None,
			TEXT("PenToUMG"),
			LOCTEXT("PenToUMGMenu", "PenToUMG"));

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
	}));

}

void FPenToUMGEditorModule::ShutdownModule()
{
	// 关闭 Pencil MCP 连接
	if (GConnector)
	{
		GConnector->Shutdown();
		GConnector.Reset();
	}

	UToolMenus::UnRegisterStartupCallback(ToolMenusStartupHandle);
	UToolMenus::UnregisterOwner(this);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("PenToUMG.Import"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("PenToUMG.PenConvert"), false);
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("PenToUMG.ImportSource"), false);

	// 反注册 Pen 数据源适配器
	FDesignSourceRegistry::Unregister(TEXT("Pen"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPenToUMGEditorModule, PenToUMGEditor)
