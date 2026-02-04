// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenVariableResolver.h"
#include "Dom/JsonValue.h"

bool FPenVariableResolver::IsVariableRef(const FString& Value)
{
	return Value.StartsWith(TEXT("$"));
}

const FPenVariable* FPenVariableResolver::GetVariable(const FString& VarName) const
{
	return Variables.Find(VarName);
}

FString FPenVariableResolver::ResolveString(const FString& Value) const
{
	if (!IsVariableRef(Value))
		return Value;

	// 去掉 $ 前缀，保留完整变量名（可能包含 --）
	FString VarName = Value.Mid(1);
	if (const FPenVariable* Var = GetVariable(VarName))
		return Var->Value;

	UE_LOG(LogTemp, Warning, TEXT("PenVariableResolver: Variable not found: %s"), *VarName);
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
