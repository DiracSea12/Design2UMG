// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include "Layout/Margin.h"
struct FUMGWidgetNode;
struct FDesignNode;
namespace UMGRetainerCapture
{
    // Fill prefixes contain the node surface and its preceding outer shadows, not its children or stroke.
    DESIGN2UMGCORE_API FVector4 FillInsets(const FDesignNode& Node);
// Preserve the parent layout slot while capturing content outside its logical box.
DESIGN2UMGCORE_API FUMGWidgetNode Wrap(FUMGWidgetNode&& Retainer,const FVector2D& LogicalSize,const FMargin& Gutter);
}
