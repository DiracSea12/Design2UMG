// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenFileImportFactory.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"

#include "WidgetBlueprint.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PenFileImportFactory"

namespace
{
	const TCHAR* DefaultOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	/** 导入单个 frame：优先预览，无预览时同步返回 Blueprint */
	UWidgetBlueprint* ImportSingleFrame(const FString& FilePath, const FString& OutputPath, bool& bOutPreviewOpened);

	/** 导入指定 TargetNodeId 对应的 frame */
	void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath);
}

// --- UFactory 实现 ---

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

	TArray<FDesignFrameInfo> Frames;
	if (!DesignImportPipeline::LoadPenFrames(Filename, Frames))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenFileImport: Failed to parse %s"), *Filename);
		FDesign2UMGNotification::Error(LOCTEXT("ParseFailed", "Design2UMG: Failed to parse .pen file"));
		return nullptr;
	}

	if (Frames.Num() == 0)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenFileImport: No frames found in %s"), *Filename);
		FDesign2UMGNotification::Error(LOCTEXT("NoFrames", "Design2UMG: No frames found in file"));
		return nullptr;
	}

	const FString OutputPath = InParent ? InParent->GetPathName() : FString(DefaultOutputPath);

	if (Frames.Num() == 1)
	{
		// 单 frame 快速路径：无预览时同步返回生成的 Blueprint
		bool bPreviewOpened = false;
		UWidgetBlueprint* Result = ImportSingleFrame(Filename, OutputPath, bPreviewOpened);
		if (Result)
			return Result;

		// 仅当预览窗口打开（异步生成）时才标记取消，其他 nullptr 情况为真正的错误/取消
		bOutOperationCanceled = bPreviewOpened;
		return nullptr;
	}

	// 多 frame：弹出选择器，异步生成
	DesignImportUtils::ShowFrameSelector(
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

// --- 内部实现 ---

namespace
{

UWidgetBlueprint* ImportSingleFrame(const FString& FilePath, const FString& OutputPath, bool& bOutPreviewOpened)
{
	bOutPreviewOpened = false;

	FDesignImportRequest Request;
	Request.OutputPath = OutputPath;
	Request.PreviewWindowTitle = LOCTEXT("PenImportPreviewTitle", "Design2UMG Import Preview");
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;

	const FDesignImportExecutionResult Result =
		DesignImportPipeline::ExecutePenFile(FilePath, FString(), Request);
	bOutPreviewOpened = Result.bPreviewOpened;
	return Result.GeneratedBlueprint;
}

void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath)
{
	FDesignImportRequest Request;
	Request.OutputPath = OutputPath;
	Request.PreviewWindowTitle = LOCTEXT("PenImportPreviewTitle", "Design2UMG Import Preview");
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;

	const FDesignImportExecutionResult Result =
		DesignImportPipeline::ExecutePenFile(FilePath, TargetNodeId, Request);
	if (!Result.bSucceeded)
	{
		FDesign2UMGNotification::Error(LOCTEXT("TargetNodeFailed", "Design2UMG: Failed to import selected frame"));
	}
}

} // anonymous namespace

#undef LOCTEXT_NAMESPACE



