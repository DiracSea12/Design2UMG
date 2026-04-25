#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaFoundation.h"

/**
 * Tunable thresholds used when projecting bounds to anchors and offsets.
 */
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

/**
 * Per-call layout projection context for slot calculation.
 */
struct FLayoutContext
{
	FIntPoint DesignSize = FIntPoint(1920, 1080);
	TOptional<float> OldAnchorX;
	TOptional<float> OldAnchorY;
	FLayoutPreset Preset;
};

/**
 * Projects fixed design-space bounds to UMG slot anchors and offsets.
 */
class DESIGN2UMGCORE_API FLayoutCalculator
{
public:
	/**
	 * Calculate the best-fit slot representation for a widget bounds rectangle.
	 * @param Bounds Design-space bounds.
	 * @param Context Projection context including design size and thresholds.
	 * @return Calculated slot data for UMG generation.
	 */
	static FUMGSlotData CalculateSlot(const FDesignPixelBounds& Bounds, const FLayoutContext& Context);
};
