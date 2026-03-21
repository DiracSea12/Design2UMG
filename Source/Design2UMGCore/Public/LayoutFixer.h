#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

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

struct FFixerLogEntry
{
	FString WidgetId;
	FString FixType;
	FBounds OldBounds;
	FBounds NewBounds;
	FString Description;
};

struct FFixerStats
{
	int32 OverflowFixTriggered = 0;
	int32 AlignSnapTriggered = 0;
	int32 SiblingSnapTriggered = 0;
	int32 EqualSpacingTriggered = 0;
	int32 SkippedExempt = 0;
	int32 RolledBack = 0;
};

struct FFixerWidgetNode
{
	FString Id;
	FString Type;
	FBounds Bounds;
	bool bFixExempt = false;
	TArray<FFixerWidgetNode> Children;
};

class DESIGN2UMGCORE_API FLayoutFixer
{
public:
	static void Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset);
	static void FixWidgetTree(FFixerWidgetNode& Root, const FFixerPreset& Preset, TArray<FFixerLogEntry>& OutLog, FFixerStats* OutStats = nullptr);
};
