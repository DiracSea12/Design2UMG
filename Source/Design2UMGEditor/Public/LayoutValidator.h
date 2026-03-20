// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

// Layout validation thresholds.
namespace EvalThresholds
{
	constexpr float Acceptance_MAE = 4.0f;
	constexpr float Acceptance_P95 = 8.0f;
}

/**
 * Layout accuracy metrics.
 */
struct FLayoutMetrics
{
	/** Mean absolute error in pixels. */
	float MAE = 0.0f;

	/** 95th percentile error in pixels. */
	float P95 = 0.0f;

	/** Number of evaluated samples. */
	int32 SampleCount = 0;

	/** Whether the metrics satisfy the acceptance thresholds. */
	bool IsAcceptable() const
	{
		return MAE <= EvalThresholds::Acceptance_MAE && P95 <= EvalThresholds::Acceptance_P95;
	}
};

/**
 * Calculates layout validation metrics such as MAE and P95.
 */
class FLayoutValidator
{
public:
	/** Calculate the aggregate bounds error for a single widget. */
	static float CalculateBoundsError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/** Calculate the position error for a single widget. */
	static float CalculatePosError(
		const FBounds& Actual,
		const FBounds& Expected,
		const FIntPoint& DesignSize);

	/** Calculate MAE and P95 for a group of bounds samples. */
	static FLayoutMetrics CalculateMetrics(
		const TArray<FBounds>& ActualBounds,
		const TArray<FBounds>& ExpectedBounds,
		const FIntPoint& DesignSize);
};
