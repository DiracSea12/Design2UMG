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
		: Variables(InVariables) {}

	void SetVariables(const TMap<FString, FPenVariable>& InVariables)
	{
		Variables = InVariables;
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
	TMap<FString, FPenVariable> Variables;

	/** 获取变量值 */
	const FPenVariable* GetVariable(const FString& VarName) const;
};
