// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignVariableResolver.h"

struct FDesignDocument;
struct FDesignNode;

/**
 * Component reference expansion: ExpandRef, deep copy, descendant overrides, and retheming.
 */
namespace RefExpander
{
	DESIGN2UMGCORE_API TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& RefNode, const FDesignDocument& Doc);
	DESIGN2UMGCORE_API void ReThemeNode(TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver);
	DESIGN2UMGCORE_API TSharedPtr<FDesignNode> DeepCopyNode(const TSharedPtr<FDesignNode>& Source);
	DESIGN2UMGCORE_API void ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const TSharedPtr<FDesignNode>& Override);
}
