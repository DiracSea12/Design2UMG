// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

/**
 * UMG 生成器 - 将解析后的数据结构转换为 Widget Blueprint
 */
class FUMGGenerator
{
public:
	/**
	 * 生成 Widget Blueprint
	 * @param WidgetData 解析后的 Widget 数据
	 * @param PackagePath 保存路径 (例如 "/Game/UI")
	 * @return 生成的 Widget Blueprint，失败返回 nullptr
	 */
	static UWidgetBlueprint* GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);
};
