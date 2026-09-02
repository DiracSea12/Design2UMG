// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"

class FJsonValue;

/**
 * Resolves design variables such as $variableName references.
 */
class DESIGN2UMGCORE_API FDesignVariableResolver
{
public:
	FDesignVariableResolver() = default;
	explicit FDesignVariableResolver(const TMap<FString, FDesignVariable>& InVariables)
		: Variables(&InVariables) {}

	void SetVariables(const TMap<FString, FDesignVariable>& InVariables)
	{
		Variables = &InVariables;
	}

	/** Set the active theme context used for themed variable selection. */
	void SetThemeContext(const TMap<FString, FString>& InTheme)
	{
		ThemeContext = InTheme;
	}

	/** Merge per-axis theme overrides into the active context; unspecified axes keep their current value. */
	void PushThemeOverride(const TMap<FString, FString>& InOverrides)
	{
		for (const auto& Pair : InOverrides)
		{
			ThemeContext.Add(Pair.Key, Pair.Value);
		}
	}

	/** Resolve a string value with variable substitution. */
	FString ResolveString(const FString& Value) const;

	/** Resolve a numeric value with variable substitution. */
	float ResolveNumber(const FString& Value) const;

	/** Resolve a boolean value with variable substitution. */
	bool ResolveBoolean(const FString& Value) const;

	/** Resolve a color value with variable substitution. */
	FString ResolveColor(const FString& Value) const;

	/** Resolve a corner-radius value from a single or four-value payload. */
	FDesignCornerRadius ResolveCornerRadius(const TSharedPtr<FJsonValue>& Value) const;

	/** Check whether a value is a variable reference. */
	static bool IsVariableRef(const FString& Value);

	/** Build the document-level active theme: the first declared value of each theme axis. */
	static TMap<FString, FString> BuildDocumentTheme(const TMap<FString, TArray<FString>>& InThemes);

private:
	const TMap<FString, FDesignVariable>* Variables = nullptr;
	TMap<FString, FString> ThemeContext;

	/** Find a variable definition by name. */
	const FDesignVariable* GetVariable(const FString& VarName) const;

	/** Resolve the active themed value for a variable. */
	FString ResolveThemedValue(const FDesignVariable& Var) const;
};
