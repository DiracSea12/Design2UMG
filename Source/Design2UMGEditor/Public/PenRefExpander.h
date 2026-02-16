// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "PenVariableResolver.h"

/**
 * 组件引用展开：ExpandRef、DeepCopy、Descendants 覆盖、ReTheme
 */
class FPenRefExpander
{
public:
	static TSharedPtr<FPenNode> ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc);
	static void ReThemeNode(TSharedPtr<FPenNode>& Node, const FPenVariableResolver& Resolver);
	static TSharedPtr<FPenNode> DeepCopyNode(const TSharedPtr<FPenNode>& Source);
	static void ApplyDescendantOverride(TSharedPtr<FPenNode>& Node, const FString& Path, const TSharedPtr<FPenNode>& Override);
};
