// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"

class FJsonObject;
class FDesignVariableResolver;
struct FDesignNode;
struct FDesignDocument;

namespace PencilScript
{
	bool BuildInputs(const FString& Source, const TSharedPtr<FJsonObject>& Overrides,
		const FDesignVariableResolver& Resolver, TSharedPtr<FJsonObject>& Out, FString& Error, TArray<FString>* OutReferences = nullptr);
	DESIGN2UMGCORE_API TArray<FString> CollectInputReferences(const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Document, const TMap<FString, FString>& Theme);
	bool ParseNodes(const FString& JSON, const FString& Scope, const FString& SourcePath,
		const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& Out, FString& Error);
	bool Expand(const TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver,
		const FString& Scope, const FString& DocumentPath, TSharedPtr<FDesignNode>& Out, FString& Error);
}
