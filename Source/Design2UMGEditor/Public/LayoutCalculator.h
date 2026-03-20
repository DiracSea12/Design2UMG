// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * Configurable thresholds used by layout calculation.
 */
struct FLayoutPreset
{
	/** Left threshold for nine-grid anchor selection. */
	float AnchorThresholdL = 0.33f;

	/** Right threshold for nine-grid anchor selection. */
	float AnchorThresholdR = 0.67f;

	/** Hysteresis buffer used to reduce anchor jitter. */
	float HysteresisBuffer = 0.05f;

	/** Edge snap threshold. */
	float EdgeSnapThreshold = 0.05f;

	/** Stretch-anchor threshold. */
	float StretchThreshold = 0.60f;

	bool bEnableNineGrid = true;
	bool bEnableHysteresis = true;
	bool bEnableEdgeSnap = true;
	bool bEnableStretchAnchors = true;

	/** Get the default HUD-oriented preset. */
	static FLayoutPreset GetStandardHUDPreset()
	{
		return FLayoutPreset();
	}
};

/**
 * Layout evaluation context.
 */
struct FLayoutContext
{
	/** Target design resolution used by UMG. */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** Previous X anchor used for hysteresis. */
	TOptional<float> OldAnchorX;

	/** Previous Y anchor used for hysteresis. */
	TOptional<float> OldAnchorY;

	/** Active layout preset. */
	FLayoutPreset Preset;
};

/**
 * Converts normalized bounds into UMG anchors and offsets.
 */
class FLayoutCalculator
{
public:
	/**
	 * Calculate slot data for a single widget.
	 * @param Bounds Normalized bounds
	 * @param Context Layout context
	 * @return Calculated slot data
	 */
	static FUMGSlotData CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context);

private:
	/** Per-axis calculation result. */
	struct FAxisResult
	{
		float AnchorMin = 0.f;
		float AnchorMax = 0.f;
		float Alignment = 0.f;
		float OffsetStart = 0.f;
		float OffsetEnd = 0.f;
	};

	/** Shared axis calculation used for both horizontal and vertical directions. */
	static FAxisResult CalculateAxis(float Pos, float Size, int32 CanvasSize, TOptional<float> OldAnchor, const FLayoutPreset& Preset);

	/** Nine-grid anchor selection with hysteresis. */
	static float SelectAnchorWithHysteresis(
		float Center,
		TOptional<float> OldAnchor,
		const FLayoutPreset& Preset);

	/**
	 * Detect whether the widget should snap to canvas edges.
	 * @param Position Normalized position in the 0..1 range
	 * @param Size Normalized size in the 0..1 range
	 * @param EdgeThreshold Edge snap threshold
	 * @param bSnapToStart Output flag for snapping to the start edge
	 * @param bSnapToEnd Output flag for snapping to the end edge
	 * @return true if edge snapping should be used
	 */
	static bool ShouldSnapToEdge(
		float Position,
		float Size,
		float EdgeThreshold,
		bool& bSnapToStart,
		bool& bSnapToEnd);

	/**
	 * Detect whether stretch anchors should be used.
	 * @param Size Normalized size in the 0..1 range
	 * @param StretchThreshold Stretch threshold
	 * @return true if stretch anchors should be used
	 */
	static bool ShouldUseStretchAnchors(float Size, float StretchThreshold);
};
