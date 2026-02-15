// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenVariableResolver.h"
#include "PenToUMGLog.h"
#include "Dom/JsonValue.h"

bool FPenVariableResolver::IsVariableRef(const FString& Value)
{
	return Value.StartsWith(TEXT("$"));
}

const FPenVariable* FPenVariableResolver::GetVariable(const FString& VarName) const
{
	return Variables ? Variables->Find(VarName) : nullptr;
}

FString FPenVariableResolver::ResolveThemedValue(const FPenVariable& Var) const
{
	if (Var.ThemedValues.Num() == 0 || ThemeContext.Num() == 0)
		return Var.Value;

	// 遍历主题值，找到与当前主题上下文匹配的
	for (const FPenThemeValue& TV : Var.ThemedValues)
	{
		if (TV.ThemeMatch.Num() == 0)
			continue;

		bool bMatch = true;
		for (const auto& Pair : TV.ThemeMatch)
		{
			const FString* ContextVal = ThemeContext.Find(Pair.Key);
			if (!ContextVal || *ContextVal != Pair.Value)
			{
				bMatch = false;
				break;
			}
		}
		if (bMatch)
			return TV.Value;
	}

	return Var.Value;
}

FString FPenVariableResolver::ResolveString(const FString& Value) const
{
	if (!IsVariableRef(Value))
		return Value;

	FString VarName = Value.Mid(1);
	if (const FPenVariable* Var = GetVariable(VarName))
		return ResolveThemedValue(*Var);

	UE_LOG(LogPenToUMG, Warning, TEXT("PenVariableResolver: Variable not found: %s"), *VarName);
	return Value;
}

float FPenVariableResolver::ResolveNumber(const FString& Value) const
{
	if (IsVariableRef(Value))
	{
		FString Resolved = ResolveString(Value);
		return FCString::Atof(*Resolved);
	}
	return FCString::Atof(*Value);
}

FString FPenVariableResolver::ResolveColor(const FString& Value) const
{
	return ResolveString(Value);
}

FPenCornerRadius FPenVariableResolver::ResolveCornerRadius(const TSharedPtr<FJsonValue>& Value) const
{
	FPenCornerRadius Result;

	if (!Value.IsValid())
		return Result;

	// 字符串格式（变量引用）
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();
		float Resolved = ResolveNumber(Str);
		Result.bUniform = true;
		Result.Uniform = Resolved;
		return Result;
	}

	// 数值格式（单值）
	if (Value->Type == EJson::Number)
	{
		Result.bUniform = true;
		Result.Uniform = Value->AsNumber();
		return Result;
	}

	// 数组格式 [左上, 右上, 右下, 左下]
	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() >= 4)
		{
			Result.bUniform = false;
			Result.TopLeft = Arr[0]->AsNumber();
			Result.TopRight = Arr[1]->AsNumber();
			Result.BottomRight = Arr[2]->AsNumber();
			Result.BottomLeft = Arr[3]->AsNumber();
		}
		return Result;
	}

	return Result;
}
