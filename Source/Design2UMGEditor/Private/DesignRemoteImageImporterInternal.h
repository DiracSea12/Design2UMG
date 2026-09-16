// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImageResourceLimits.h"

namespace DesignRemoteImageImporterInternal
{
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
		DesignImageResources::FImageResourceLimits ImageLimits;
		DesignImageResources::FImageOperationBudget OperationBudget{ ImageLimits };
		FResolveReport Report;
		TMap<FString, FString> UrlToAssetPath;
		TMap<FString, FString> ContentHashToAsset;
		TMap<FString, FUrlDownloadResult> UrlDownloads;
	};

	class FImagePayloadIO
	{
	public:
		static bool ValidateCompressedByteCount(uint64 ByteCount, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateDimensions(int32 Width, int32 Height, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateUrlCount(int32 UrlCount, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateRemoteUrl(const FString& Url, bool bAllowUnsafeRemoteUrls, FString& OutError);
		static bool LoadLocalFile(
			const FString& FilePath,
			const DesignImageResources::FImageResourceLimits& Limits,
			TArray<uint8>& OutData,
			FString& OutError);
		// Downloads all URLs concurrently: every request is started first, then a
		// single wait loop ticks the HttpManager until all requests complete.
		// TimeoutSeconds is a per-request budget: each request gets the full
		// window measured from its own start, and the wait loop cancels a request
		// once its own deadline elapses (so a large batch no longer starves its
		// tail requests). Batch duration stays bounded because requests run
		// concurrently and the interactive slow task remains cancellable.
		// Synchronous; fills one result per URL.
		static void DownloadUrls(
			const TArray<FString>& Urls,
			float TimeoutSeconds,
			bool bAllowUnsafeRemoteUrls,
			const DesignImageResources::FImageResourceLimits& Limits,
			TMap<FString, FUrlDownloadResult>& OutResults);
		static bool DecodeToBGRA8(
			const TArray<uint8>& CompressedData,
			const DesignImageResources::FImageResourceLimits& Limits,
			int32& OutWidth,
			int32& OutHeight,
			TArray64<uint8>& OutRawData,
			FString& OutError,
			DesignImageResources::FImageOperationBudget* OperationBudget = nullptr);
	};
}
