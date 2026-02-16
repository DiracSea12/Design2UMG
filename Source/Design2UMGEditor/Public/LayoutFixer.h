// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

/**
 * Fixer配置参数
 */
struct FFixerPreset
{
	/** 设计尺寸 */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** 动态吸附阈值（像素），运行时根据DesignSize计算 */
	int32 SnapPx = 4;

	/** 行/列检测的重叠度阈值 */
	float RowOverlapThreshold = 0.8f;

	/** 等距修正最小元素数 */
	int32 MinElementsForEqualSpacing = 3;

	/** 等距修正最大间距方差（归一化），超过则不修正 */
	float MaxSpacingVarianceForEqualSpacing = 0.02f;

	bool bEnableOverflowFix = true;
	bool bEnableAlignSnap = true;
	bool bEnableSiblingSnap = true;
	bool bEnableEqualSpacing = true;

};

/**
 * 修正日志条目
 */
struct FFixerLogEntry
{
	FString WidgetId;
	FString FixType;      // "Overflow", "Snap", "EqualSpacing"
	FBounds OldBounds;
	FBounds NewBounds;
	FString Description;
};

/**
 * v2.3: Fixer触发统计
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
 * Widget节点（用于Fixer处理）
 */
struct FFixerWidgetNode
{
	FString Id;
	FString Type;
	FBounds Bounds;
	bool bFixExempt = false;  // v2.2: 禁止修正标记
	TArray<FFixerWidgetNode> Children;
};

/**
 * 布局修复器 - 纯规则层自动修复
 * 执行位置：在LayoutCalc之前，直接修改Bounds
 */
class FLayoutFixer
{
public:
	/**
	 * 初始化Fixer，计算动态阈值
	 */
	static void Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset);

	/**
	 * 修复整个Widget树
	 * @param OutStats v2.3: 可选的统计输出
	 */
	static void FixWidgetTree(
		FFixerWidgetNode& Root,
		const FFixerPreset& Preset,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats* OutStats = nullptr);

	/**
	 * 越界修正：确保子元素不超出父容器
	 */
	static bool FixBoundsOverflow(
		FBounds& ChildBounds,
		const FBounds& ParentBounds,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 对齐吸附：吸附到父边缘/中线
	 */
	static bool SnapToAlignment(
		FBounds& Bounds,
		const FBounds& ParentBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 同级元素对齐吸附
	 */
	static bool SnapToSiblings(
		FBounds& Bounds,
		const TArray<FBounds>& SiblingBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 检测同一行的元素（Y轴重叠度>阈值视为同一行）
	 */
	static TArray<TArray<int32>> DetectRows(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/**
	 * 检测同一列的元素（X轴重叠度>阈值视为同一列）
	 */
	static TArray<TArray<int32>> DetectColumns(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/**
	 * 组内等距修正
	 */
	static bool FixEqualSpacing(
		TArray<FBounds>& Bounds,
		const TArray<int32>& Indices,
		bool bHorizontal,
		float Threshold,
		TArray<FFixerLogEntry>* OutLog = nullptr);

private:
	static float GetAverage(const TArray<float>& Values);
	static float GetMaxDeviation(const TArray<float>& Values, float Average);

	/** 递归处理单个节点及其子树 */
	static void ProcessNode(
		FFixerWidgetNode& Node,
		const FBounds& ParentBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** 修正子节点的同级吸附和等距间距 */
	static void FixChildrenSpacing(
		TArray<FBounds>& ProposedBounds,
		const TArray<FBounds>& OriginalBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** 保守性检查：验证修正幅度是否合理 */
	static bool PassesConservatismCheck(
		const TArray<FBounds>& OriginalBounds,
		const TArray<FBounds>& ProposedBounds,
		float SnapThresholdX,
		float SnapThresholdY);
};
