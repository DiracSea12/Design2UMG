// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"

struct FLayoutPreset
{
	float AnchorThresholdL = 0.33f;
	float AnchorThresholdR = 0.67f;
	float HysteresisBuffer = 0.05f;
	float EdgeSnapThreshold = 0.05f;
	float StretchThreshold = 0.60f;
	bool bEnableNineGrid = true;
	bool bEnableHysteresis = true;
	bool bEnableEdgeSnap = true;
	bool bEnableStretchAnchors = true;

	static FLayoutPreset GetStandardHUDPreset() { return FLayoutPreset(); }
};

struct FLayoutContext
{
	FIntPoint DesignSize = FIntPoint::ZeroValue;
	TOptional<float> OldAnchorX;
	TOptional<float> OldAnchorY;
	FLayoutPreset Preset;
};

namespace LayoutCalculator
{
	DESIGN2UMGCORE_API FUMGSlotData CalculateSlot(const FDesignPixelBounds& Bounds, const FLayoutContext& Context);
}
