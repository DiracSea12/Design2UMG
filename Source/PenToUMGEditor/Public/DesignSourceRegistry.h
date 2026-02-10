// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * 设计数据源适配器函数签名
 *
 * 输入: 原始 JSON 字符串（各数据源自己的格式）
 * 输出: FUMGWidgetData（统一中间表示）
 */
using FDesignSourceConvertFunc = TFunction<bool(const FString& RawJson, FUMGWidgetData& OutData)>;

/**
 * 设计数据源注册表
 *
 * 各适配器在模块 StartupModule 中调用 Register() 注册，
 * ShutdownModule 中调用 Unregister() 反注册。
 */
class FDesignSourceRegistry
{
public:
	static void Register(FName SourceId, FDesignSourceConvertFunc Func);
	static void Unregister(FName SourceId);
	static bool Convert(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
	static bool IsRegistered(FName SourceId);
	static TArray<FName> GetRegisteredSources();

private:
	static TMap<FName, FDesignSourceConvertFunc>& GetRegistry();
};
