// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenFileImportFactory.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenImporter.h"
#include "PenImportUtils.h"
#include "DesignSourceRegistry.h"

#include "WidgetBlueprint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PenFileImportFactory"

namespace
{
	const TCHAR* DefaultOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

	/** 导入单 frame：尝试预览，无预览时同步返回 Blueprint */
	UWidgetBlueprint* ImportSingleFrame(const FString& FilePath, const FString& OutputPath);

	/** 导入指定 TargetNodeId 的 frame */
	void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath);
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

	const FString OutputPath = InParent ? InParent->GetPathName() : FString(DefaultOutputPath);

	if (Frames.Num() == 1)
	{
		// 单 frame 快速路径：无预览时同步返回生成的 Blueprint
		UWidgetBlueprint* Result = ImportSingleFrame(Filename, OutputPath);
		if (Result)
			return Result;

		// 返回 nullptr 说明预览窗口已打开，异步生成
		bOutOperationCanceled = true;
		return nullptr;
	}

	// 多 frame → 弹出选择器，异步生成
	PenImportUtils::ShowFrameSelector(
		Frames,
		LOCTEXT("FrameSelectorTitle", "Select Frame to Import"),
		LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
		[Filename, OutputPath](const FString& FrameId)
		{
			ImportWithTargetNode(Filename, FrameId, OutputPath);
		});
	bOutOperationCanceled = true;
	return nullptr;
}

// ── 内部实现 ──

namespace
{

UWidgetBlueprint* ImportSingleFrame(const FString& FilePath, const FString& OutputPath)
{
	FUMGWidgetData Data;
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to read file: %s"), *FilePath);
		return nullptr;
	}
	if (!FDesignSourceRegistry::Convert(TEXT("Pen"), JsonStr, Data))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Convert failed: %s"), *FilePath);
		return nullptr;
	}

	PenImportUtils::ResolveImages(Data);

	if (PenImportUtils::OpenPreviewWindow(Data, OutputPath,
		LOCTEXT("PenImportPreviewTitle", "PenToUMG Import Preview")))
	{
		return nullptr;
	}

	return PenImportUtils::GenerateBlueprint(Data, OutputPath);
}

void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath)
{
	FUMGWidgetData Data;
	if (!FPenImporter::ImportFileWithTargetNode(FilePath, TargetNodeId, Data))
		return;

	PenImportUtils::ResolveImages(Data);

	if (!PenImportUtils::OpenPreviewWindow(Data, OutputPath,
		LOCTEXT("PenImportPreviewTitle", "PenToUMG Import Preview")))
	{
		PenImportUtils::GenerateBlueprint(Data, OutputPath);
	}
}

} // anonymous namespace

#undef LOCTEXT_NAMESPACE
