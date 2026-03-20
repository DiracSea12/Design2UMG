// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

/**
 * Configurable parameters for the layout fixer.
 */
struct FFixerPreset
{
	/** Design resolution. */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** Dynamic snap threshold in pixels, computed from DesignSize at runtime. */
	int32 SnapPx = 4;

	/** Overlap threshold used when detecting rows and columns. */
	float RowOverlapThreshold = 0.8f;

	/** Minimum element count required for equal-spacing correction. */
	int32 MinElementsForEqualSpacing = 3;

	/** Maximum normalized spacing variance allowed for equal-spacing correction. */
	float MaxSpacingVarianceForEqualSpacing = 0.02f;

	bool bEnableOverflowFix = true;
	bool bEnableAlignSnap = true;
	bool bEnableSiblingSnap = true;
	bool bEnableEqualSpacing = true;
};

/**
 * Single fixer log entry.
 */
struct FFixerLogEntry
{
	FString WidgetId;
	FString FixType;
	FBounds OldBounds;
	FBounds NewBounds;
	FString Description;
};

/**
 * Fixer trigger statistics.
 */
struct FFixerStats
{
	int32 OverflowFixTriggered = 0;
	int32 AlignSnapTriggered = 0;
	int32 SiblingSnapTriggered = 0;
	int32 EqualSpacingTriggered = 0;
	int32 SkippedExempt = 0;
	int32 RolledBack = 0;
};

/**
 * Simplified widget node used by the layout fixer.
 */
struct FFixerWidgetNode
{
	FString Id;
	FString Type;
	FBounds Bounds;
	bool bFixExempt = false;
	TArray<FFixerWidgetNode> Children;
};

/**
 * Rule-based layout fixer that adjusts bounds before layout calculation.
 */
class FLayoutFixer
{
public:
	/** Initialize the fixer and compute dynamic thresholds. */
	static void Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset);

	/**
	 * Fix an entire widget tree.
	 * @param OutStats Optional stats output
	 */
	static void FixWidgetTree(
		FFixerWidgetNode& Root,
		const FFixerPreset& Preset,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats* OutStats = nullptr);

	/** Ensure a child does not overflow its parent bounds. */
	static bool FixBoundsOverflow(
		FBounds& ChildBounds,
		const FBounds& ParentBounds,
		FFixerLogEntry* OutLog = nullptr);

	/** Snap a widget to parent edges or center lines. */
	static bool SnapToAlignment(
		FBounds& Bounds,
		const FBounds& ParentBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/** Snap a widget to sibling alignment cues. */
	static bool SnapToSiblings(
		FBounds& Bounds,
		const TArray<FBounds>& SiblingBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/** Detect groups of widgets that form a row. */
	static TArray<TArray<int32>> DetectRows(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/** Detect groups of widgets that form a column. */
	static TArray<TArray<int32>> DetectColumns(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/** Apply equal-spacing correction inside a row or column group. */
	static bool FixEqualSpacing(
		TArray<FBounds>& Bounds,
		const TArray<int32>& Indices,
		bool bHorizontal,
		float Threshold,
		TArray<FFixerLogEntry>* OutLog = nullptr);

private:
	static float GetAverage(const TArray<float>& Values);
	static float GetMaxDeviation(const TArray<float>& Values, float Average);

	/** Recursively process a node and its subtree. */
	static void ProcessNode(
		FFixerWidgetNode& Node,
		const FBounds& ParentBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** Fix sibling snapping and equal spacing for child bounds. */
	static void FixChildrenSpacing(
		TArray<FBounds>& ProposedBounds,
		const TArray<FBounds>& OriginalBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** Conservative safety check for proposed adjustments. */
	static bool PassesConservatismCheck(
		const TArray<FBounds>& OriginalBounds,
		const TArray<FBounds>& ProposedBounds,
		float SnapThresholdX,
		float SnapThresholdY);
};
