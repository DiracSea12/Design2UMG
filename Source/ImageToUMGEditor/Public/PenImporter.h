// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FUMGWidgetData;

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
};
