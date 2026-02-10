// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGEditorModule.h"
#include "PenToUMGLog.h"
#include "PenMCPBridge.h"
#include "PenRemoteImageImporter.h"
#include "SPenToUMGPreviewWindow.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGWidgetUtils.h"

#include "PenParser.h"
#include "PenImporter.h"
#include "DesignSourceRegistry.h"

#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SWindow.h"
#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

DEFINE_LOG_CATEGORY(LogPenToUMG);

#define LOCTEXT_NAMESPACE "FPenToUMGEditorModule"

namespace
{
	const TCHAR* DefaultPenOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

	struct FPenConvertCommandArgs
	{
		FString NodesPath;
		FString VarsPath;
		FString TargetNodeId;
		FString OutputPath = DefaultPenOutputPath;
		bool bUsePreview = true;
	};

	TSharedRef<FJsonObject> SerializeAnchor(const FUMGAnchorData& Anchor)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("X"), Anchor.X);
		Json->SetNumberField(TEXT("Y"), Anchor.Y);
		return Json;
	}

	TSharedRef<FJsonObject> SerializeAnchors(const FUMGAnchorsData& Anchors)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("Min"), SerializeAnchor(Anchors.Min));
		Json->SetObjectField(TEXT("Max"), SerializeAnchor(Anchors.Max));
		return Json;
	}

	TSharedRef<FJsonObject> SerializeOffsets(const FUMGOffsetsData& Offsets)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("Left"), Offsets.Left);
		Json->SetNumberField(TEXT("Top"), Offsets.Top);
		Json->SetNumberField(TEXT("Right"), Offsets.Right);
		Json->SetNumberField(TEXT("Bottom"), Offsets.Bottom);
		return Json;
	}

	TSharedRef<FJsonObject> SerializeSlot(const FUMGSlotData& Slot)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("Anchors"), SerializeAnchors(Slot.Anchors));
		Json->SetObjectField(TEXT("Alignment"), SerializeAnchor(Slot.Alignment));
		Json->SetObjectField(TEXT("Offsets"), SerializeOffsets(Slot.Offsets));

		const bool bHasPadding =
			!FMath::IsNearlyEqual(Slot.Padding.Left, 0.0f) ||
			!FMath::IsNearlyEqual(Slot.Padding.Top, 0.0f) ||
			!FMath::IsNearlyEqual(Slot.Padding.Right, 0.0f) ||
			!FMath::IsNearlyEqual(Slot.Padding.Bottom, 0.0f);
		if (bHasPadding)
		{
			Json->SetStringField(TEXT("Padding"), FString::Printf(
				TEXT("%.2f,%.2f,%.2f,%.2f"),
				Slot.Padding.Left,
				Slot.Padding.Top,
				Slot.Padding.Right,
				Slot.Padding.Bottom));
		}

		if (Slot.HorizontalAlignment != TEXT("Fill"))
		{
			Json->SetStringField(TEXT("HorizontalAlignment"), Slot.HorizontalAlignment);
		}
		if (Slot.VerticalAlignment != TEXT("Fill"))
		{
			Json->SetStringField(TEXT("VerticalAlignment"), Slot.VerticalAlignment);
		}
		if (Slot.SizeRule != TEXT("Auto"))
		{
			Json->SetStringField(TEXT("SizeRule"), Slot.SizeRule);
		}
		if (!FMath::IsNearlyEqual(Slot.FillWeight, 1.0f))
		{
			Json->SetNumberField(TEXT("FillWeight"), Slot.FillWeight);
		}
		if (Slot.Row != 0)
		{
			Json->SetNumberField(TEXT("Row"), Slot.Row);
		}
		if (Slot.Column != 0)
		{
			Json->SetNumberField(TEXT("Column"), Slot.Column);
		}
		if (Slot.RowSpan != 1)
		{
			Json->SetNumberField(TEXT("RowSpan"), Slot.RowSpan);
		}
		if (Slot.ColumnSpan != 1)
		{
			Json->SetNumberField(TEXT("ColumnSpan"), Slot.ColumnSpan);
		}

		return Json;
	}

	TSharedRef<FJsonObject> SerializeBounds(const FBounds& Bounds)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("X"), Bounds.X);
		Json->SetNumberField(TEXT("Y"), Bounds.Y);
		Json->SetNumberField(TEXT("W"), Bounds.W);
		Json->SetNumberField(TEXT("H"), Bounds.H);
		return Json;
	}

	TSharedRef<FJsonObject> SerializeWidgetNode(const FUMGWidgetNode& Node)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("Type"), Node.Type);
		if (!Node.Id.IsEmpty())
		{
			Json->SetStringField(TEXT("Id"), Node.Id);
		}
		if (!Node.Name.IsEmpty())
		{
			Json->SetStringField(TEXT("Name"), Node.Name);
		}

		const bool bHasBounds =
			!FMath::IsNearlyZero(Node.Bounds.X) ||
			!FMath::IsNearlyZero(Node.Bounds.Y) ||
			!FMath::IsNearlyZero(Node.Bounds.W) ||
			!FMath::IsNearlyZero(Node.Bounds.H);
		if (bHasBounds)
		{
			Json->SetObjectField(TEXT("Bounds"), SerializeBounds(Node.Bounds));
		}

		Json->SetObjectField(TEXT("Slot"), SerializeSlot(Node.Slot));

		if (Node.Properties.Num() > 0)
		{
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			for (const TPair<FString, FString>& Pair : Node.Properties)
			{
				Props->SetStringField(Pair.Key, Pair.Value);
			}
			Json->SetObjectField(TEXT("Properties"), Props);
		}

		if (Node.Children.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ChildArray;
			ChildArray.Reserve(Node.Children.Num());
			for (const FUMGWidgetNode& Child : Node.Children)
			{
				ChildArray.Add(MakeShared<FJsonValueObject>(SerializeWidgetNode(Child)));
			}
			Json->SetArrayField(TEXT("Children"), ChildArray);
		}

		return Json;
	}

	FString SerializeWidgetDataToJson(const FUMGWidgetData& Data)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("WidgetName"), Data.WidgetName);
		Root->SetObjectField(TEXT("RootWidget"), SerializeWidgetNode(Data.RootWidget));

		FString Output;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Root, Writer);
		return Output;
	}

	bool GenerateWidgetAndPreviewPng(const FUMGWidgetData& Data, const FString& OutputPath)
	{
		UWidgetBlueprint* WidgetBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
		if (!WidgetBP)
		{
			UE_LOG(LogPenToUMG, Error, TEXT("[FAIL] Widget Blueprint generation failed"));
			return false;
		}

		UE_LOG(LogPenToUMG, Log, TEXT("[OK] Widget Blueprint generated: %s"), *WidgetBP->GetPathName());

		TArray<uint8> PngData;
		FIntPoint Size;
		if (!FUMGWidgetUtils::RenderWidgetToPng(WidgetBP, FIntPoint(400, 300), PngData, Size))
		{
			UE_LOG(LogPenToUMG, Error, TEXT("[FAIL] Widget PNG render failed"));
			return false;
		}

		const FString OutPng = FPaths::ProjectSavedDir() / TEXT("PenToUMG/rendered.png");
		FFileHelper::SaveArrayToFile(PngData, *OutPng);
		UE_LOG(LogPenToUMG, Log, TEXT("[OK] Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
		return true;
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

	void RunGenerateFromJson(const FString& EditedJson, const FString& OutputPath)
	{
		FUMGWidgetData EditedData;
		FString ParseError;
		if (!FUMGJsonParser::ParseJson(EditedJson, EditedData, &ParseError))
		{
			UE_LOG(LogPenToUMG, Error, TEXT("[FAIL] Edited JSON parse failed: %s"), *ParseError);
			return;
		}

		FString RemoteImageReport;
		const bool bRemoteImagesResolved = FPenRemoteImageImporter::ResolveRemoteImages(EditedData, &RemoteImageReport);
		if (!RemoteImageReport.IsEmpty())
		{
			UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
		}
		if (!bRemoteImagesResolved)
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("Edited JSON contains unresolved remote images; generation will continue"));
		}

		const FString FinalOutputPath = OutputPath.IsEmpty() ? FString(DefaultPenOutputPath) : OutputPath;
		GenerateWidgetAndPreviewPng(EditedData, FinalOutputPath);
	}

	bool TryOpenPreviewWindow(const FUMGWidgetData& Data, const FString& OutputPath)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		const FString JsonPayload = SerializeWidgetDataToJson(Data);
		const FString InitialOutputPath = OutputPath.IsEmpty() ? FString(DefaultPenOutputPath) : OutputPath;

		TSharedRef<SWindow> PreviewWindow = SNew(SWindow)
			.Title(LOCTEXT("PenConvertPreviewWindowTitle", "PenToUMG Pen Preview"))
			.ClientSize(FVector2D(1600.0f, 980.0f))
			.SupportsMinimize(false)
			.SupportsMaximize(true);

		PreviewWindow->SetContent(
			SNew(SPenToUMGPreviewWindow)
			.OriginalJson(JsonPayload)
			.OutputPath(InitialOutputPath)
			.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
				[](const FString& EditedJson, const FString& ConfirmedOutputPath)
				{
					RunGenerateFromJson(EditedJson, ConfirmedOutputPath);
				}))
			.OnCancelled(FOnPreviewCancelled::CreateLambda(
				[]()
				{
					UE_LOG(LogPenToUMG, Log, TEXT("PenConvert preview cancelled"));
				}))
		);

		FSlateApplication::Get().AddWindow(PreviewWindow);
		return true;
	}

	/** 导入 .pen 文件（工具栏 + 控制台命令共用） */
	void ImportPenFile(const FString& FilePath, const FString& OutputPath, bool bUsePreview = true)
	{
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

		FString RemoteImageReport;
		FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
		if (!RemoteImageReport.IsEmpty())
		{
			UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
		}

		if (bUsePreview && TryOpenPreviewWindow(Data, OutputPath))
		{
			return;
		}

		GenerateWidgetAndPreviewPng(Data, OutputPath);
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

			FString RemoteImageReport;
			const bool bRemoteImagesResolved = FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
			if (!RemoteImageReport.IsEmpty())
			{
				UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
			}
			if (!bRemoteImagesResolved)
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("PenConvert contains unresolved remote images; generation will continue"));
			}

			UE_LOG(LogPenToUMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
			for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
			{
				const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
				UE_LOG(LogPenToUMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
			}

			if (CmdArgs.bUsePreview && TryOpenPreviewWindow(Data, CmdArgs.OutputPath))
			{
				return;
			}

			GenerateWidgetAndPreviewPng(Data, CmdArgs.OutputPath);
		}),
		ECVF_Default);

	// PenToUMG.Import 命令
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PenToUMG.Import"),
		TEXT("Import .pen file: PenToUMG.Import <file.pen> [--out=/Game/Path] [--no-preview]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("Usage: PenToUMG.Import <file.pen> [--out=/Game/Path] [--no-preview]"));
				return;
			}

			const FString FilePath = Args[0];
			FString OutputPath = DefaultPenOutputPath;
			bool bPreview = true;

			for (int32 i = 1; i < Args.Num(); ++i)
			{
				if (Args[i].StartsWith(TEXT("--out=")))
					OutputPath = Args[i].RightChop(6);
				else if (Args[i] == TEXT("--no-preview"))
					bPreview = false;
			}

			if (!FPaths::FileExists(FilePath))
			{
				UE_LOG(LogPenToUMG, Error, TEXT("File not found: %s"), *FilePath);
				return;
			}

			ImportPenFile(FilePath, OutputPath, bPreview);
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

			if (bPreview && TryOpenPreviewWindow(Data, OutputPath))
			{
				return;
			}

			GenerateWidgetAndPreviewPng(Data, OutputPath);
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
				UE_LOG(LogPenToUMG, Log, TEXT("Connect to Pencil: Not yet implemented (Path B)"));
			})));
	}));

}

void FPenToUMGEditorModule::ShutdownModule()
{
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
