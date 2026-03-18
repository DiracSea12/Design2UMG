// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * 甯冨眬棰勮 - 鍙厤缃殑闃堝€煎弬鏁? */
struct FLayoutPreset
{
	/** 9瀹牸宸﹂槇鍊硷紙榛樿0.33锛?*/
	float AnchorThresholdL = 0.33f;

	/** 9瀹牸鍙抽槇鍊硷紙榛樿0.67锛?*/
	float AnchorThresholdR = 0.67f;

	/** Hysteresis缂撳啿鍖猴紙榛樿0.05锛?*/
	float HysteresisBuffer = 0.05f;

	/** 杈圭紭鍚搁檮闃堝€硷紙榛樿0.05锛?*/
	float EdgeSnapThreshold = 0.05f;

	/** 鎷変几閿氱偣闃堝€硷紙榛樿0.60锛?*/
	float StretchThreshold = 0.60f;

	/** Enable nine-grid anchor selection */
	bool bEnableNineGrid = true;
	bool bEnableHysteresis = true;
	bool bEnableEdgeSnap = true;
	bool bEnableStretchAnchors = true;

	/** 鑾峰彇鏍囧噯HUD棰勮 */
	static FLayoutPreset GetStandardHUDPreset()
	{
		return FLayoutPreset();
	}
};

/**
 * 甯冨眬涓婁笅鏂?- 璁＄畻鏃剁殑鐜淇℃伅
 */
struct FLayoutContext
{
	/** 璁捐鍒嗚鲸鐜囷紙UMG鐩爣锛?*/
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** 涓婁竴杞殑閿氱偣X锛堢敤浜嶩ysteresis锛?*/
	TOptional<float> OldAnchorX;

	/** 涓婁竴杞殑閿氱偣Y锛堢敤浜嶩ysteresis锛?*/
	TOptional<float> OldAnchorY;

	/** 甯冨眬棰勮 */
	FLayoutPreset Preset;
};

// FBounds宸插湪UMGSchema.h涓畾涔?
/**
 * 甯冨眬璁＄畻鍣?- 灏咮ounds杞崲涓篣MG鐨凙nchors/Offsets
 */
class FLayoutCalculator
{
public:
	/**
	 * 璁＄畻鍗曚釜Widget鐨凷lot鏁版嵁
	 * @param Bounds 褰掍竴鍖栬竟鐣屾
	 * @param Context 甯冨眬涓婁笅鏂?	 * @return 璁＄畻寰楀埌鐨凷lot鏁版嵁
	 */
	static FUMGSlotData CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context);

private:
	/** 鍗曡酱璁＄畻缁撴灉 */
	struct FAxisResult
	{
		float AnchorMin = 0.f;
		float AnchorMax = 0.f;
		float Alignment = 0.f;
		float OffsetStart = 0.f;  // Left or Top
		float OffsetEnd = 0.f;    // Right or Bottom
	};

	/** 鍗曡酱 Slot 璁＄畻锛堟按骞?鍨傜洿鍏辩敤锛?*/
	static FAxisResult CalculateAxis(float Pos, float Size, int32 CanvasSize, TOptional<float> OldAnchor, const FLayoutPreset& Preset);

	/**
	 * 9瀹牸閿氱偣閫夋嫨锛堝甫Hysteresis闃茶烦鍙橈級
	 */
	static float SelectAnchorWithHysteresis(
		float Center,
		TOptional<float> OldAnchor,
		const FLayoutPreset& Preset
	);

	/**
	 * 杈圭紭鍚搁檮妫€娴?	 * @param Position 鍏冪礌浣嶇疆锛?-1锛?	 * @param Size 鍏冪礌灏哄锛?-1锛?	 * @param EdgeThreshold 杈圭紭闃堝€?	 * @param bSnapToStart 杈撳嚭锛氭槸鍚﹀惛闄勫埌璧峰杈圭紭
	 * @param bSnapToEnd 杈撳嚭锛氭槸鍚﹀惛闄勫埌缁撴潫杈圭紭
	 * @return 鏄惁闇€瑕佽竟缂樺惛闄?	 */
	static bool ShouldSnapToEdge(
		float Position,
		float Size,
		float EdgeThreshold,
		bool& bSnapToStart,
		bool& bSnapToEnd
	);

	/**
	 * 鎷変几閿氱偣妫€娴?	 * @param Size 鍏冪礌灏哄锛?-1锛?	 * @param StretchThreshold 鎷変几闃堝€?	 * @return 鏄惁搴斾娇鐢ㄦ媺浼搁敋鐐?	 */
	static bool ShouldUseStretchAnchors(float Size, float StretchThreshold);
};

