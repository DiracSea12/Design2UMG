// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

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
	 * 生成 Widget Blueprint（构建 + 编译 + 保存）
	 * @param WidgetData 解析后的 Widget 数据
	 * @param PackagePath 保存路径 (例如 "/Game/UI")
	 * @return 生成的 Widget Blueprint，失败返回 nullptr
	 */
	static UWidgetBlueprint* GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** 构建 + 编译 Widget Blueprint，但不保存到磁盘。transient 纹理此时仍在内存中。 */
	static UWidgetBlueprint* BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** 将已编译的 Widget Blueprint 保存到磁盘。 */
	static void SaveWidgetBlueprint(UWidgetBlueprint* WBP);
};
