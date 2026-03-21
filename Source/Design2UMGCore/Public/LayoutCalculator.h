#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

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
	FIntPoint DesignSize = FIntPoint(1920, 1080);
	TOptional<float> OldAnchorX;
	TOptional<float> OldAnchorY;
	FLayoutPreset Preset;
};

class DESIGN2UMGCORE_API FLayoutCalculator
{
public:
	static FUMGSlotData CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context);
};
