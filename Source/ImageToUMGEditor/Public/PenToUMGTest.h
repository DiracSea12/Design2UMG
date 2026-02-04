// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * PenToUMG 单元测试
 */
class FPenToUMGTest
{
public:
	/** 运行所有测试 */
	static bool RunAllTests();

	/** 测试颜色解析 */
	static bool TestParseColor();

	/** 测试尺寸解析 */
	static bool TestParseSize();

	/** 测试变量解析 */
	static bool TestVariableResolver();

	/** 测试节点转换 */
	static bool TestNodeConversion();

	/** 测试布局转换 */
	static bool TestLayoutConversion();

private:
	static void LogResult(const FString& TestName, bool bPassed);
};
