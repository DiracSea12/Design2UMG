// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignImageResourceLimits.h"
#include "Design2UMGSettings.h"

namespace DesignImageResources
{
	FImageResourceLimits GetGeneratedTextureLimits()
	{
		FImageResourceLimits Limits;
		// Config files bypass the editor's ClampMin; keep their values positive too.
		Limits.MaxTotalDecodedBytes = static_cast<uint64>(FMath::Max(1, UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB)) * 1024ull * 1024ull;
		return Limits;
	}

	namespace
	{
		constexpr uint64 Bgra8BytesPerPixel = 4;
		bool TryAddWithinLimit(const uint64 Current, const uint64 Increment, const uint64 Limit, uint64& OutTotal)
		{
			if (Increment > Limit || Current > Limit - Increment)
			{
				return false;
			}
			OutTotal = Current + Increment;
			return true;
		}

	}

	bool FImageResourceLimits::TryGetPixelSize(float Width, float Height, FIntPoint& OutSize) const
	{
		OutSize = FIntPoint::ZeroValue;
		if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width <= 0.0f || Height <= 0.0f
			|| static_cast<double>(Width) > MaxDimension || static_cast<double>(Height) > MaxDimension) return false;
		const int64 PixelWidth = FMath::Max<int64>(1, FMath::RoundToInt64(Width));
		const int64 PixelHeight = FMath::Max<int64>(1, FMath::RoundToInt64(Height));
		if (!AllowsDimensions(PixelWidth, PixelHeight)) return false;
		OutSize = FIntPoint(static_cast<int32>(PixelWidth), static_cast<int32>(PixelHeight));
		return true;
	}

	bool FImageOperationBudget::ConsumeCompressedBytes(const uint64 ByteCount, FString& OutError)
	{
		uint64 NewTotal = 0;
		if (!TryAddWithinLimit(TotalCompressedBytes, ByteCount, Limits.MaxTotalCompressedBytes, NewTotal))
		{
			OutError = FString::Printf(
				TEXT("Total compressed image data exceeds the operation limit (%llu bytes)"),
				static_cast<unsigned long long>(Limits.MaxTotalCompressedBytes));
			if (RefusalError.IsEmpty()) RefusalError = OutError;
			return false;
		}
		TotalCompressedBytes = NewTotal;
		OutError.Reset();
		return true;
	}

	bool FImageOperationBudget::ConsumeDecodedPixels(const int64 PixelCount, FString& OutError)
	{
		if (PixelCount < 0 || static_cast<uint64>(PixelCount) > TNumericLimits<uint64>::Max() / Bgra8BytesPerPixel)
		{
			OutError = TEXT("Decoded image size is invalid");
			if (RefusalError.IsEmpty()) RefusalError = OutError;
			return false;
		}

		const uint64 DecodedBytes = static_cast<uint64>(PixelCount) * Bgra8BytesPerPixel;
		uint64 NewTotal = 0;
		if (!TryAddWithinLimit(TotalDecodedBytes, DecodedBytes, Limits.MaxTotalDecodedBytes, NewTotal))
		{
			OutError = FString::Printf(
				TEXT("Total decoded image data exceeds the operation limit (%llu bytes)"),
				static_cast<unsigned long long>(Limits.MaxTotalDecodedBytes));
			if (RefusalError.IsEmpty()) RefusalError = OutError;
			return false;
		}
		TotalDecodedBytes = NewTotal;
		OutError.Reset();
		return true;
	}
}
