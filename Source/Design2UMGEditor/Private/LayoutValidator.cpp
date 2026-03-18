// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "LayoutValidator.h"

float FLayoutValidator::CalculateBoundsError(
	const FBounds& Actual,
	const FBounds& Expected,
	const FIntPoint& DesignSize)
{
	// Convert normalized error back into pixels.
	float ErrorX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
	float ErrorY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
	float ErrorW = FMath::Abs(Actual.W - Expected.W) * DesignSize.X;
	float ErrorH = FMath::Abs(Actual.H - Expected.H) * DesignSize.Y;

	// 杩斿洖浣嶇疆+灏哄骞冲潎璇樊
	float PosError = (ErrorX + ErrorY) * 0.5f;
	float SizeError = (ErrorW + ErrorH) * 0.5f;
	return FMath::Max(PosError, SizeError);
}

float FLayoutValidator::CalculatePosError(
	const FBounds& Actual,
	const FBounds& Expected,
	const FIntPoint& DesignSize)
{
	const float ErrorX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
	const float ErrorY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
	return (ErrorX + ErrorY) * 0.5f;
}

FLayoutMetrics FLayoutValidator::CalculateMetrics(
	const TArray<FBounds>& ActualBounds,
	const TArray<FBounds>& ExpectedBounds,
	const FIntPoint& DesignSize)
{
	FLayoutMetrics Metrics;

	int32 Count = FMath::Min(ActualBounds.Num(), ExpectedBounds.Num());
	if (Count == 0)
	{
		return Metrics;
	}

	TArray<float> Errors;
	Errors.Reserve(Count);

	float TotalError = 0.0f;
	for (int32 i = 0; i < Count; ++i)
	{
		float Error = CalculateBoundsError(ActualBounds[i], ExpectedBounds[i], DesignSize);
		Errors.Add(Error);
		TotalError += Error;
	}

	// MAE
	Metrics.MAE = TotalError / Count;
	Metrics.SampleCount = Count;

	// P95 (linear interpolation for consistency with other tools)
	Errors.Sort();
	const float RealIndex = (Errors.Num() - 1) * 0.95f;
	const int32 LowIndex = FMath::Clamp(FMath::FloorToInt(RealIndex), 0, Errors.Num() - 1);
	const int32 HighIndex = FMath::Clamp(LowIndex + 1, 0, Errors.Num() - 1);
	const float Frac = RealIndex - LowIndex;
	Metrics.P95 = FMath::Lerp(Errors[LowIndex], Errors[HighIndex], Frac);

	return Metrics;
}

