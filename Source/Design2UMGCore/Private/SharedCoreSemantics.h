// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

namespace Design2UMGCore::SharedSemantics
{
	bool HasVisibleFill(const FDesignFill& Fill);
	bool IsDesignerOnlyNodeType(EDesignNodeType Type);
	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node);
	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node);
	bool TryGetConcreteSourceTextBoxSize(const TSharedPtr<FDesignNode>& Node, float& OutWidth, float& OutHeight);
	float ResolveWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
}
