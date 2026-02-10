// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenFileImportFactory.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenImporter.h"
#include "PenRemoteImageImporter.h"
#include "DesignSourceRegistry.h"
#include "SPenToUMGPreviewWindow.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGWidgetUtils.h"
#include "PenToUMGSettings.h"

#include "WidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PenFileImportFactory"

// ── 前向声明内部函数 ──

namespace PenImportInternal
{
	static const TCHAR* DefaultOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

	/** 执行导入（单 frame 快速路径） */
	static void ImportSingleFrame(const FString& FilePath, const FString& OutputPath);

	/** 执行导入（多 frame 选择路径） */
	static void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath);

	/** 打开预览窗口 */
	static bool OpenPreview(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 生成 Widget Blueprint + 渲染 PNG */
	static void GenerateAndRender(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 从编辑后的 JSON 生成 */
	static void RunGenerateFromJson(const FString& EditedJson, const FString& OutputPath);

	/** 弹出 Frame 选择器对话框 */
	static void ShowFrameSelector(const FString& FilePath, const TArray<FPenFrameInfo>& Frames, const FString& OutputPath);
}

// ── UFactory 实现 ──

UPenFileImportFactory::UPenFileImportFactory()
{
	bEditorImport = true;
	bText = false;
	SupportedClass = UWidgetBlueprint::StaticClass();
	Formats.Add(TEXT("pen;Pencil Design File"));
}

bool UPenFileImportFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("pen"), ESearchCase::IgnoreCase);
}

UObject* UPenFileImportFactory::FactoryCreateFile(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn,
	bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	// 解析文档，检查顶层 frame 数量
	FPenDocument Document;
	if (!FPenParser::ParseFromFile(Filename, Document))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to parse %s"), *Filename);
		return nullptr;
	}

	TArray<FPenFrameInfo> Frames = ::ExtractTopFrames(Document);

	if (Frames.Num() == 0)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: No frames found in %s"), *Filename);
		return nullptr;
	}

	const FString OutputPath = InParent ? InParent->GetPathName() : FString(PenImportInternal::DefaultOutputPath);

	if (Frames.Num() == 1)
	{
		// 单 frame 快速路径
		PenImportInternal::ImportSingleFrame(Filename, OutputPath);
	}
	else
	{
		// 多 frame → 弹出选择器
		PenImportInternal::ShowFrameSelector(Filename, Frames, OutputPath);
	}

	// UFactory 返回 nullptr，实际生成在预览确认后异步完成
	bOutOperationCanceled = false;
	return nullptr;
}

// ── 内部实现 ──

namespace PenImportInternal
{

void ImportSingleFrame(const FString& FilePath, const FString& OutputPath)
{
	FUMGWidgetData Data;
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to read file: %s"), *FilePath);
		return;
	}
	if (!FDesignSourceRegistry::Convert(TEXT("Pen"), JsonStr, Data))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Convert failed: %s"), *FilePath);
		return;
	}

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
	}

	if (!OpenPreview(Data, OutputPath))
	{
		GenerateAndRender(Data, OutputPath);
	}
}

void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath)
{
	FUMGWidgetData Data;
	if (!FPenImporter::ImportFileWithTargetNode(FilePath, TargetNodeId, Data))
		return;

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);

	if (!OpenPreview(Data, OutputPath))
		GenerateAndRender(Data, OutputPath);
}

void RunGenerateFromJson(const FString& EditedJson, const FString& OutputPath)
{
	FUMGWidgetData EditedData;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(EditedJson, EditedData, &ParseError))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: JSON parse failed: %s"), *ParseError);
		return;
	}

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(EditedData, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
	}

	GenerateAndRender(EditedData, OutputPath);
}

void GenerateAndRender(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const FString FinalPath = OutputPath.IsEmpty() ? FString(DefaultOutputPath) : OutputPath;
	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Widget Blueprint generation failed"));
		return;
	}
	UE_LOG(LogPenToUMG, Log, TEXT("PenFileImport: Generated %s"), *WBP->GetPathName());
}

// ── OpenPreview ──

bool OpenPreview(const FUMGWidgetData& Data, const FString& OutputPath)
{
	if (!FSlateApplication::IsInitialized())
		return false;

	const FString JsonPayload = FUMGJsonSerializer::Serialize(Data);
	const FString FinalPath = OutputPath.IsEmpty() ? FString(DefaultOutputPath) : OutputPath;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("PenImportPreviewTitle", "PenToUMG Import Preview"))
		.ClientSize(FVector2D(1600.0f, 980.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(true);

	Window->SetContent(
		SNew(SPenToUMGPreviewWindow)
		.OriginalJson(JsonPayload)
		.OutputPath(FinalPath)
		.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
			[](const FString& EditedJson, const FString& ConfirmedPath)
			{
				RunGenerateFromJson(EditedJson, ConfirmedPath);
			}))
		.OnCancelled(FOnPreviewCancelled::CreateLambda(
			[]()
			{
				UE_LOG(LogPenToUMG, Log, TEXT("PenFileImport: Preview cancelled"));
			}))
	);

	FSlateApplication::Get().AddWindow(Window);
	return true;
}

// ── ShowFrameSelector ──

void ShowFrameSelector(const FString& FilePath, const TArray<FPenFrameInfo>& Frames, const FString& OutputPath)
{
	if (!FSlateApplication::IsInitialized())
		return;

	// 构建列表数据（SListView 需要 TSharedPtr）
	TSharedPtr<TArray<TSharedPtr<FPenFrameInfo>>> Items = MakeShared<TArray<TSharedPtr<FPenFrameInfo>>>();
	for (const FPenFrameInfo& F : Frames)
		Items->Add(MakeShared<FPenFrameInfo>(F));

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("FrameSelectorTitle", "Select Frame to Import"))
		.ClientSize(FVector2D(420.0f, 360.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	// 捕获变量供 lambda 使用
	FString CapturedFilePath = FilePath;
	FString CapturedOutputPath = OutputPath;
	TWeakPtr<SWindow> WeakWindow = Window;

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SNew(SListView<TSharedPtr<FPenFrameInfo>>)
			.ListItemsSource(Items.Get())
			.OnGenerateRow_Lambda([Items](TSharedPtr<FPenFrameInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
			{
				return SNew(STableRow<TSharedPtr<FPenFrameInfo>>, OwnerTable)
					.Padding(FMargin(4.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s  (%.0f x %.0f)"),
							*Item->Name, Item->Width, Item->Height)))
					];
			})
			.OnMouseButtonDoubleClick_Lambda([CapturedFilePath, CapturedOutputPath, WeakWindow, Items](TSharedPtr<FPenFrameInfo> Item)
			{
				if (Item.IsValid())
				{
					ImportWithTargetNode(CapturedFilePath, Item->Id, CapturedOutputPath);
					if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						Win->RequestDestroyWindow();
				}
			})
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

} // namespace PenImportInternal

#undef LOCTEXT_NAMESPACE
