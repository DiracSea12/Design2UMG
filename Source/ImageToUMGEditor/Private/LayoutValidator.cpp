// Copyright Epic Games, Inc. All Rights Reserved.

#include "LayoutValidator.h"

float FLayoutValidator::CalculateBoundsError(
	const FBounds& Actual,
	const FBounds& Expected,
	const FIntPoint& DesignSize)
{
	// 计算X/Y/W/H四个维度的误差，转换为像素
	float ErrorX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
	float ErrorY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
	float ErrorW = FMath::Abs(Actual.W - Expected.W) * DesignSize.X;
	float ErrorH = FMath::Abs(Actual.H - Expected.H) * DesignSize.Y;

	// 返回平均误差
	return (ErrorX + ErrorY + ErrorW + ErrorH) / 4.0f;
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

	// P95
	Errors.Sort();
	int32 P95Index = FMath::FloorToInt(Count * 0.95f);
	P95Index = FMath::Clamp(P95Index, 0, Count - 1);
	Metrics.P95 = Errors[P95Index];

	return Metrics;
}
