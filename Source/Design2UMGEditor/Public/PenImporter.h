// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

struct FImportResult
{
	bool bSuccess = false;
	int32 WidgetCount = 0;
	int32 SkippedNodeCount = 0;
	int32 WarningCount = 0;
	double ElapsedSeconds = 0.0;
	TArray<FString> Warnings;

	static int32 CountWidgets(const FUMGWidgetNode& Node);
	static FImportResult FromData(const FUMGWidgetData& Data, double Elapsed);
	void ShowSummaryNotification() const;
	void ShowSummaryDialog() const;
};

/**
 * .pen 文件导入入口
 */
class FPenImporter
{
public:
	/** 从文件导入 .pen 并转换为 UMG */
	static bool ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData);

	/** 从 JSON 字符串导入 */
	static bool ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData);

	/** 从 .pen 文件导入指定 TargetNodeId 的 frame（多 frame 选择路径） */
	static bool ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData);
};
