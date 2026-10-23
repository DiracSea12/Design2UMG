// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

// Image allocation, shadow working-set and widget export policies.
namespace DesignImageResources
{
	inline constexpr int64 RenderExportMaxPixels = 256ll * 1024ll * 1024ll;
	inline constexpr int32 RenderTileEdge = 8192;

	inline constexpr uint64 ShadowFilterMaxWorkingBytes = 512ull * 1024ull * 1024ull;
	inline constexpr int64 ShadowFilterMaxOperations = 1ll << 30;
	// At most 8191 float coefficients, below 32 KiB.
	inline constexpr int32 ShadowFilterMaxKernelRadius = 4095;

	enum class EImageDimensionError : uint8
	{
		None,
		NonPositive,
		SideLimitExceeded,
		PixelLimitExceeded
	};

	struct FImageResourceLimits
	{
		static constexpr uint64 DefaultMaxCompressedBytes = 32ull * 1024ull * 1024ull;
		static constexpr int32 DefaultMaxDimension = 16384;
		static constexpr int64 DefaultMaxPixels = 64ll * 1024ll * 1024ll;
		static constexpr int32 DefaultMaxUrlsPerOperation = 128;
		static constexpr uint64 DefaultMaxTotalCompressedBytes = DefaultMaxCompressedBytes * 8ull;
		// Import allowance; generated textures use their separate editor preference.
		static constexpr uint64 DefaultMaxTotalDecodedBytes = 512ull * 1024ull * 1024ull;

		uint64 MaxCompressedBytes = DefaultMaxCompressedBytes;
		int32 MaxDimension = DefaultMaxDimension;
		int64 MaxPixels = DefaultMaxPixels;
		int32 MaxUrlsPerOperation = DefaultMaxUrlsPerOperation;
		uint64 MaxTotalCompressedBytes = DefaultMaxTotalCompressedBytes;
		uint64 MaxTotalDecodedBytes = DefaultMaxTotalDecodedBytes;

		EImageDimensionError GetDimensionError(int64 Width, int64 Height) const;
		bool AllowsDimensions(int64 Width, int64 Height) const
		{
			return GetDimensionError(Width, Height) == EImageDimensionError::None;
		}
		/** Validate before narrowing source dimensions to integer pixels. */
		bool TryGetPixelSize(float Width, float Height, FIntPoint& OutSize) const;
	};

	/** Read editor preferences once when a generated-texture operation starts. */
	FImageResourceLimits GetGeneratedTextureLimits();

	class FImageOperationBudget
	{
	public:
		explicit FImageOperationBudget(const FImageResourceLimits& InLimits)
			: Limits(InLimits)
		{
		}

		bool ConsumeCompressedBytes(uint64 ByteCount, FString& OutError);
		bool ConsumeDecodedPixels(int64 PixelCount, FString& OutError);

		/** First allocation refusal in this operation; successful calls leave it intact. */
		const FString& GetRefusalError() const { return RefusalError; }

	private:
		FString RefusalError;
		FImageResourceLimits Limits;
		uint64 TotalCompressedBytes = 0;
		uint64 TotalDecodedBytes = 0;
	};

}
