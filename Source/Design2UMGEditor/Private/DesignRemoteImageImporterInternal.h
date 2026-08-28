// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignRemoteImageImporterInternal
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

	struct FResolveReport
	{
		int32 ImportedCount = 0;
		int32 ReusedCount = 0;
		int32 RepairedCount = 0;
		int32 FailedCount = 0;
		TArray<FString> Messages;

		void AddMessage(const FString& Message);
		FString BuildSummary(const FString& ImportPath) const;
	};

	struct FUrlDownloadResult
	{
		bool bSuccess = false;
		TArray<uint8> Data;
		FString Error;
	};

	struct FResolveContext
	{
		FString ImportPath;
		FString FrameName;
		float TimeoutSeconds = 20.0f;
		bool bResolveRemoteImages = false;
		bool bAllowUnsafeRemoteUrls = false;
		bool bTextureResourceUpdatePending = false;
		FImageResourceLimits ImageLimits;
		FImageOperationBudget OperationBudget{ ImageLimits };
		FResolveReport Report;
		TMap<FString, FString> UrlToAssetPath;
		TMap<FString, FString> ContentHashToAsset;
		TMap<FString, FUrlDownloadResult> UrlDownloads;
	};

	class FImagePayloadIO
	{
	public:
		static bool ValidateCompressedByteCount(uint64 ByteCount, const FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateDimensions(int32 Width, int32 Height, const FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateUrlCount(int32 UrlCount, const FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateRemoteUrl(const FString& Url, bool bAllowUnsafeRemoteUrls, FString& OutError);
		static bool LoadLocalFile(
			const FString& FilePath,
			const FImageResourceLimits& Limits,
			TArray<uint8>& OutData,
			FString& OutError);
		// Downloads all URLs concurrently: every request is started first, then a
		// single deadline loop ticks the HttpManager until all requests complete.
		// The deadline spans the whole batch (TimeoutSeconds measured from batch
		// start, not per URL). Synchronous; fills one result per URL.
		static void DownloadUrls(
			const TArray<FString>& Urls,
			float TimeoutSeconds,
			bool bAllowUnsafeRemoteUrls,
			const FImageResourceLimits& Limits,
			TMap<FString, FUrlDownloadResult>& OutResults);
		static bool DecodeToBGRA8(
			const TArray<uint8>& CompressedData,
			const FImageResourceLimits& Limits,
			int32& OutWidth,
			int32& OutHeight,
			TArray64<uint8>& OutRawData,
			FString& OutError,
			FImageOperationBudget* OperationBudget = nullptr);
	};
}
