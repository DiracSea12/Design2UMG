// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"

/**
 * 变量解析器 - 解析 $variableName 格式的变量引用
 */
class FPenVariableResolver
{
public:
	FPenVariableResolver() = default;
	explicit FPenVariableResolver(const TMap<FString, FPenVariable>& InVariables)
		: Variables(&InVariables) {}

	void SetVariables(const TMap<FString, FPenVariable>& InVariables)
	{
		Variables = &InVariables;
	}

	/** 设置当前主题上下文，用于选择正确的主题值 */
	void SetThemeContext(const TMap<FString, FString>& InTheme)
	{
		ThemeContext = InTheme;
	}

	/** 解析字符串值，替换变量引用 */
	FString ResolveString(const FString& Value) const;

	/** 解析数值，支持变量引用 */
	float ResolveNumber(const FString& Value) const;

	/** 解析颜色，支持变量引用 */
	FString ResolveColor(const FString& Value) const;

	/** 解析圆角，支持单值和四值数组 */
	FPenCornerRadius ResolveCornerRadius(const TSharedPtr<FJsonValue>& Value) const;

	/** 检查是否为变量引用 */
	static bool IsVariableRef(const FString& Value);

private:
	const TMap<FString, FPenVariable>* Variables = nullptr;
	TMap<FString, FString> ThemeContext;

	/** 获取变量值 */
	const FPenVariable* GetVariable(const FString& VarName) const;

	/** 根据当前主题上下文从变量的主题值数组中选择正确的值 */
	FString ResolveThemedValue(const FPenVariable& Var) const;
};
