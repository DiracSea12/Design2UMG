// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

/**
 * Fixer閰嶇疆鍙傛暟
 */
struct FFixerPreset
{
	/** 璁捐灏哄 */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** 鍔ㄦ€佸惛闄勯槇鍊硷紙鍍忕礌锛夛紝杩愯鏃舵牴鎹瓺esignSize璁＄畻 */
	int32 SnapPx = 4;

	/** 琛?鍒楁娴嬬殑閲嶅彔搴﹂槇鍊?*/
	float RowOverlapThreshold = 0.8f;

	/** 绛夎窛淇鏈€灏忓厓绱犳暟 */
	int32 MinElementsForEqualSpacing = 3;

	/** 绛夎窛淇鏈€澶ч棿璺濇柟宸紙褰掍竴鍖栵級锛岃秴杩囧垯涓嶄慨姝?*/
	float MaxSpacingVarianceForEqualSpacing = 0.02f;

	bool bEnableOverflowFix = true;
	bool bEnableAlignSnap = true;
	bool bEnableSiblingSnap = true;
	bool bEnableEqualSpacing = true;

};

/**
 * 淇鏃ュ織鏉＄洰
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
 * v2.3: Fixer瑙﹀彂缁熻
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
 * Widget鑺傜偣锛堢敤浜嶧ixer澶勭悊锛? */
struct FFixerWidgetNode
{
	FString Id;
	FString Type;
	FBounds Bounds;
	bool bFixExempt = false;  // v2.2: 绂佹淇鏍囪
	TArray<FFixerWidgetNode> Children;
};

/**
 * 甯冨眬淇鍣?- 绾鍒欏眰鑷姩淇
 * 鎵ц浣嶇疆锛氬湪LayoutCalc涔嬪墠锛岀洿鎺ヤ慨鏀笲ounds
 */
class FLayoutFixer
{
public:
	/**
	 * 鍒濆鍖朏ixer锛岃绠楀姩鎬侀槇鍊?
	 */
	static void Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset);

	/**
	 * 淇鏁翠釜Widget鏍?
	 * @param OutStats v2.3: 鍙€夌殑缁熻杈撳嚭
	 */
	static void FixWidgetTree(
		FFixerWidgetNode& Root,
		const FFixerPreset& Preset,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats* OutStats = nullptr);

	/**
	 * 瓒婄晫淇锛氱‘淇濆瓙鍏冪礌涓嶈秴鍑虹埗瀹瑰櫒
	 */
	static bool FixBoundsOverflow(
		FBounds& ChildBounds,
		const FBounds& ParentBounds,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 瀵归綈鍚搁檮锛氬惛闄勫埌鐖惰竟缂?涓嚎
	 */
	static bool SnapToAlignment(
		FBounds& Bounds,
		const FBounds& ParentBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 鍚岀骇鍏冪礌瀵归綈鍚搁檮
	 */
	static bool SnapToSiblings(
		FBounds& Bounds,
		const TArray<FBounds>& SiblingBounds,
		float SnapThresholdX,
		float SnapThresholdY,
		FFixerLogEntry* OutLog = nullptr);

	/**
	 * 妫€娴嬪悓涓€琛岀殑鍏冪礌锛圷杞撮噸鍙犲害>闃堝€艰涓哄悓涓€琛岋級
	 */
	static TArray<TArray<int32>> DetectRows(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/**
	 * 妫€娴嬪悓涓€鍒楃殑鍏冪礌锛圶杞撮噸鍙犲害>闃堝€艰涓哄悓涓€鍒楋級
	 */
	static TArray<TArray<int32>> DetectColumns(
		const TArray<FBounds>& Bounds,
		float OverlapThreshold);

	/**
	 * 缁勫唴绛夎窛淇
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

	/** 閫掑綊澶勭悊鍗曚釜鑺傜偣鍙婂叾瀛愭爲 */
	static void ProcessNode(
		FFixerWidgetNode& Node,
		const FBounds& ParentBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** 淇瀛愯妭鐐圭殑鍚岀骇鍚搁檮鍜岀瓑璺濋棿璺?*/
	static void FixChildrenSpacing(
		TArray<FBounds>& ProposedBounds,
		const TArray<FBounds>& OriginalBounds,
		const FFixerPreset& Preset,
		float SnapThresholdX,
		float SnapThresholdY,
		TArray<FFixerLogEntry>& OutLog,
		FFixerStats& Stats);

	/** 淇濆畧鎬ф鏌ワ細楠岃瘉淇骞呭害鏄惁鍚堢悊 */
	static bool PassesConservatismCheck(
		const TArray<FBounds>& OriginalBounds,
		const TArray<FBounds>& ProposedBounds,
		float SnapThresholdX,
		float SnapThresholdY);
};

