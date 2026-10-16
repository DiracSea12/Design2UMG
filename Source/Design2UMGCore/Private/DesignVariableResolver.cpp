// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignVariableResolver.h"
#include "DesignColorUtils.h"
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

		// Imported names may have numeric aliases, e.g. "$5:--background".
		// A bare numeric dollar value such as "$45.00" remains literal text.
		int32 NameStart = 1;
		if (FChar::IsDigit(Value[1]))
		{
			const int32 Colon = Value.Find(TEXT(":"));
			if (Colon == INDEX_NONE || Colon + 1 >= Value.Len()) return false;
			NameStart = Colon + 1;
		}
		const TCHAR FirstChar = Value[NameStart];
		if (!FChar::IsAlpha(FirstChar) && FirstChar != TEXT('_') && FirstChar != TEXT('-'))
		{
			return false;
		}

		for (int32 Index = 2; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-') || Char == TEXT('/') || Char == TEXT('.') || Char == TEXT(':'))
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
	// Apply variable entries in order: unconditional entries always match; themed entries
	// match the active axis. Last match wins. Alias-qualified axes override unprefixed axes.
	FString Current = Var.Value;
	for (const FDesignThemeValue& TV : Var.ThemedValues)
	{
		bool bApplies = true;
		for (const auto& Pair : TV.ThemeMatch)
		{
			const FString* ContextVal = nullptr;
			if (!Var.SourceAlias.IsEmpty())
			{
				ContextVal = ThemeContext.Find(Var.SourceAlias + TEXT(":") + Pair.Key);
			}
			if (!ContextVal)
			{
				ContextVal = ThemeContext.Find(Pair.Key);
			}
			if (!ContextVal || *ContextVal != Pair.Value)
			{
				bApplies = false;
				break;
			}
		}
		if (bApplies)
		{
			Current = TV.Value;
		}
	}

	return Current;
}

TMap<FString, FString> FDesignVariableResolver::BuildDocumentTheme(const TMap<FString, TArray<FString>>& InThemes)
{
	// The document-level active theme is the first declared value of each axis,
	// matching the Pencil editor's default state when a node declares no theme.
	TMap<FString, FString> DocTheme;
	for (const auto& Pair : InThemes)
	{
		if (Pair.Value.Num() > 0)
		{
			DocTheme.Add(Pair.Key, Pair.Value[0]);
		}
	}
	return DocTheme;
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

FString FDesignVariableResolver::ResolveColor(const FString& Value) const
{
	return DesignColorUtils::NormalizeLiteral(ResolveString(Value));
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
