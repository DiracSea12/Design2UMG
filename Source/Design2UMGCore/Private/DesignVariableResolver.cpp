// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignVariableResolver.h"
#include "Design2UMGLog.h"
#include "Dom/JsonValue.h"

namespace
{
	bool IsLikelyVariableName(const FString& Value)
	{
		if (!Value.StartsWith(TEXT("$")) || Value.Len() <= 1)
		{
			return false;
		}

		for (int32 Index = 1; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-'))
			{
				continue;
			}
			return false;
		}

		return true;
	}

	bool TryResolveCornerNumber(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver, float& OutValue)
	{
		if (!Value.IsValid())
			return false;

		if (Value->Type == EJson::Number)
		{
			OutValue = Value->AsNumber();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			OutValue = Resolver.ResolveNumber(Value->AsString());
			return true;
		}

		return false;
	}
}

bool FDesignVariableResolver::IsVariableRef(const FString& Value)
{
	return IsLikelyVariableName(Value);
}

const FDesignVariable* FDesignVariableResolver::GetVariable(const FString& VarName) const
{
	return Variables ? Variables->Find(VarName) : nullptr;
}

FString FDesignVariableResolver::ResolveThemedValue(const FDesignVariable& Var) const
{
	if (Var.ThemedValues.Num() == 0 || ThemeContext.Num() == 0)
		return Var.Value;

	// Select the first themed value whose theme axes match current context.
	for (const FDesignThemeValue& TV : Var.ThemedValues)
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

FString FDesignVariableResolver::ResolveString(const FString& Value) const
{
	if (!IsVariableRef(Value))
		return Value;

	FString VarName = Value.Mid(1);
	if (const FDesignVariable* Var = GetVariable(VarName))
		return ResolveThemedValue(*Var);

	UE_LOG(LogDesign2UMG, Warning, TEXT("DesignVariableResolver: Variable not found: %s"), *VarName);
	return Value;
}

float FDesignVariableResolver::ResolveNumber(const FString& Value) const
{
	if (IsVariableRef(Value))
	{
		FString Resolved = ResolveString(Value);
		return FCString::Atof(*Resolved);
	}
	return FCString::Atof(*Value);
}

bool FDesignVariableResolver::ResolveBoolean(const FString& Value) const
{
	FString Resolved = IsVariableRef(Value) ? ResolveString(Value) : Value;
	Resolved = Resolved.TrimStartAndEnd().ToLower();

	if (Resolved == TEXT("true") || Resolved == TEXT("1") || Resolved == TEXT("yes") || Resolved == TEXT("on"))
		return true;
	if (Resolved == TEXT("false") || Resolved == TEXT("0") || Resolved == TEXT("no") || Resolved == TEXT("off"))
		return false;

	return false;
}

FString FDesignVariableResolver::ResolveColor(const FString& Value) const
{
	return ResolveString(Value);
}

FDesignCornerRadius FDesignVariableResolver::ResolveCornerRadius(const TSharedPtr<FJsonValue>& Value) const
{
	FDesignCornerRadius Result;

	if (!Value.IsValid())
		return Result;

	if (Value->Type == EJson::String)
	{
		const FString Str = Value->AsString();
		const float Resolved = ResolveNumber(Str);
		Result.bUniform = true;
		Result.Uniform = Resolved;
		return Result;
	}

	if (Value->Type == EJson::Number)
	{
		Result.bUniform = true;
		Result.Uniform = Value->AsNumber();
		return Result;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() >= 4)
		{
			Result.bUniform = false;
			TryResolveCornerNumber(Arr[0], *this, Result.TopLeft);
			TryResolveCornerNumber(Arr[1], *this, Result.TopRight);
			TryResolveCornerNumber(Arr[2], *this, Result.BottomRight);
			TryResolveCornerNumber(Arr[3], *this, Result.BottomLeft);
		}
		else if (Arr.Num() == 1)
		{
			Result.bUniform = true;
			TryResolveCornerNumber(Arr[0], *this, Result.Uniform);
		}
		else if (Arr.Num() >= 2)
		{
			Result.bUniform = false;
			TryResolveCornerNumber(Arr[0], *this, Result.TopLeft);
			TryResolveCornerNumber(Arr[1], *this, Result.TopRight);
			Result.BottomRight = Result.TopLeft;
			if (Arr.Num() > 2)
				TryResolveCornerNumber(Arr[2], *this, Result.BottomRight);
			Result.BottomLeft = Result.TopRight;
		}
		return Result;
	}

	return Result;
}



