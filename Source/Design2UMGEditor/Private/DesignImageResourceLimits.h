// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

// One image budget policy for decoded inputs and generated texture resources.
namespace DesignImageResources
{
	struct FImageResourceLimits
	{
		static constexpr uint64 DefaultMaxCompressedBytes = 32ull * 1024ull * 1024ull;
		static constexpr int32 DefaultMaxDimension = 16384;
		static constexpr int64 DefaultMaxPixels = 64ll * 1024ll * 1024ll;
		static constexpr int32 DefaultMaxUrlsPerOperation = 128;
		static constexpr uint64 DefaultMaxTotalCompressedBytes = DefaultMaxCompressedBytes * 8ull;
		static constexpr uint64 DefaultMaxTotalDecodedBytes = 512ull * 1024ull * 1024ull;

		uint64 MaxCompressedBytes = DefaultMaxCompressedBytes;
		int32 MaxDimension = DefaultMaxDimension;
		int64 MaxPixels = DefaultMaxPixels;
		int32 MaxUrlsPerOperation = DefaultMaxUrlsPerOperation;
		uint64 MaxTotalCompressedBytes = DefaultMaxTotalCompressedBytes;
		uint64 MaxTotalDecodedBytes = DefaultMaxTotalDecodedBytes;

		bool AllowsDimensions(int64 Width, int64 Height) const
		{
			return Width > 0 && Height > 0 && Width <= MaxDimension && Height <= MaxDimension
				&& Width * Height <= MaxPixels;
		}
		/** Validate before narrowing source dimensions to integer pixels. */
		bool TryGetPixelSize(float Width, float Height, FIntPoint& OutSize) const;
	};

	class FImageOperationBudget
	{
	public:
		explicit FImageOperationBudget(const FImageResourceLimits& InLimits)
			: Limits(InLimits)
		{
		}

		bool ConsumeCompressedBytes(uint64 ByteCount, FString& OutError);
		bool ConsumeDecodedPixels(int64 PixelCount, FString& OutError);

	private:
		FImageResourceLimits Limits;
		uint64 TotalCompressedBytes = 0;
		uint64 TotalDecodedBytes = 0;
	};

}
