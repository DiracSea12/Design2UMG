// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"

struct FLayoutContext
{
	FIntPoint DesignSize = FIntPoint::ZeroValue;
	TOptional<float> OldAnchorX;
	TOptional<float> OldAnchorY;
};

namespace LayoutCalculator
{
	DESIGN2UMGCORE_API FUMGSlotData CalculateSlot(const FDesignPixelBounds& Bounds, const FLayoutContext& Context);
}
