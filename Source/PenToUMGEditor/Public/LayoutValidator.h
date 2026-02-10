// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

// 布局验收阈值常量
namespace EvalThresholds
{
	constexpr float Acceptance_MAE = 4.0f;
	constexpr float Acceptance_P95 = 8.0f;
}

/**
 * 布局精度指标
 */
struct FLayoutMetrics
{
	/** 平均绝对误差（像素） */
	float MAE = 0.0f;

	/** 95分位误差（像素） */
	float P95 = 0.0f;

	/** 样本数量 */
	int32 SampleCount = 0;

	/** 是否通过验收 */
	bool IsAcceptable() const
	{
		return MAE <= EvalThresholds::Acceptance_MAE && P95 <= EvalThresholds::Acceptance_P95;
	}
};

/**
 * 布局验证器 - 计算MAE/P95指标
 */
class FLayoutValidator
{
public:
	/**
	 * 计算单个Bounds的误差（像素）
	 */
	static float CalculateBoundsError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/**
	 * 计算单个Bounds的位置误差（像素）
	 */
	static float CalculatePosError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/**
	 * 计算一组样本的MAE/P95指标
	 */
	static FLayoutMetrics CalculateMetrics(
		const TArray<FBounds>& ActualBounds,
		const TArray<FBounds>& ExpectedBounds,
		const FIntPoint& DesignSize);
};
