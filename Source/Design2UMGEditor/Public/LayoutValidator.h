// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

// 甯冨眬楠屾敹闃堝€煎父閲?
namespace EvalThresholds
{
	constexpr float Acceptance_MAE = 4.0f;
	constexpr float Acceptance_P95 = 8.0f;
}

/**
 * 甯冨眬绮惧害鎸囨爣
 */
struct FLayoutMetrics
{
	/** 骞冲潎缁濆璇樊锛堝儚绱狅級 */
	float MAE = 0.0f;

	/** 95鍒嗕綅璇樊锛堝儚绱狅級 */
	float P95 = 0.0f;

	/** 鏍锋湰鏁伴噺 */
	int32 SampleCount = 0;

	/** 鏄惁閫氳繃楠屾敹 */
	bool IsAcceptable() const
	{
		return MAE <= EvalThresholds::Acceptance_MAE && P95 <= EvalThresholds::Acceptance_P95;
	}
};

/**
 * 甯冨眬楠岃瘉鍣?- 璁＄畻MAE/P95鎸囨爣
 */
class FLayoutValidator
{
public:
	/**
	 * 璁＄畻鍗曚釜Bounds鐨勮宸紙鍍忕礌锛?
	 */
	static float CalculateBoundsError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/**
	 * 璁＄畻鍗曚釜Bounds鐨勪綅缃宸紙鍍忕礌锛?
	 */
	static float CalculatePosError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/**
	 * 璁＄畻涓€缁勬牱鏈殑MAE/P95鎸囨爣
	 */
	static FLayoutMetrics CalculateMetrics(
		const TArray<FBounds>& ActualBounds,
		const TArray<FBounds>& ExpectedBounds,
		const FIntPoint& DesignSize);
};

