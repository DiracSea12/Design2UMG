#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

/**
 * Thresholds and switches that control bounds-level layout repair passes.
 */
struct FFixerPreset
{
	FIntPoint DesignSize = FIntPoint(1920, 1080);
	int32 SnapPx = 4;
	float RowOverlapThreshold = 0.8f;
	int32 MinElementsForEqualSpacing = 3;
	float MaxSpacingVarianceForEqualSpacing = 0.02f;
	bool bEnableOverflowFix = true;
	bool bEnableAlignSnap = true;
	bool bEnableSiblingSnap = true;
	bool bEnableEqualSpacing = true;
};

/**
 * A single layout repair event recorded during fixer execution.
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
 * Aggregate counters describing which repair passes ran.
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
 * Minimal fixer-side tree representation used for bounds repair.
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
 * Applies rule-based bounds repairs before final anchor/offset projection.
 */
class DESIGN2UMGCORE_API FLayoutFixer
{
public:
	/**
	 * Initialize a fixer preset from the current design size.
	 * @param DesignSize Design canvas size in pixels.
	 * @param OutPreset Output preset to initialize.
	 */
	static void Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset);

	/**
	 * Fix a widget tree in place and optionally emit logs and counters.
	 * @param Root Root fixer node to mutate.
	 * @param Preset Repair thresholds and toggles.
	 * @param OutLog Output repair log entries.
	 * @param OutStats Optional aggregate counters.
	 */
	static void FixWidgetTree(FFixerWidgetNode& Root, const FFixerPreset& Preset, TArray<FFixerLogEntry>& OutLog, FFixerStats* OutStats = nullptr);
};
