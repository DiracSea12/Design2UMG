// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"

/**
 * 组件引用展开：ExpandRef、DeepCopy、Descendants 覆盖、ReTheme
 */
class FRefExpander
{
public:
	static TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& RefNode, const FDesignDocument& Doc);
	static void ReThemeNode(TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver);
	static TSharedPtr<FDesignNode> DeepCopyNode(const TSharedPtr<FDesignNode>& Source);
	static void ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const TSharedPtr<FDesignNode>& Override);
};


