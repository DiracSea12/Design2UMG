// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImageDiagnostics.h"
#include "DesignImageResourceLimits.h"

namespace DesignRemoteImageImporterInternal
{
	struct FResolveReport
	{
		FDesignImageImportResult Result;

		void AddDiagnostic(
			EDesignImageDiagnosticOutcome Outcome,
			EDesignImageDiagnosticCategory Category,
			const FString& NodeId,
			const FString& NodeName,
			const FString& Resource,
			const FString& Reason,
			const FString& Message);
		FDesignImageImportResult BuildResult(const FString& ImportPath);
	};

	struct FUrlDownloadResult
	{
		bool bSuccess = false;
		TArray<uint8> Data;
		FString Error;
		EDesignImageDiagnosticCategory FailureCategory = EDesignImageDiagnosticCategory::Download;

		/** Transfers the body to its consumer so this cached result stops retaining it. */
		TArray<uint8> TakePayload()
		{
			return MoveTemp(Data);
		}
	};

	struct FImageProcessingFailure
	{
		EDesignImageDiagnosticCategory Category = EDesignImageDiagnosticCategory::Decode;
		FString Reason;
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
		// Processing failures are cached separately from transport results. This lets a
		// repeated node report the same failure without retaining or re-decoding the
		// compressed payload after the first attempt has consumed it.
		TMap<FString, FImageProcessingFailure> UrlProcessingErrors;
	};

	class FImagePayloadIO
	{
	public:
		static bool ValidateCompressedByteCount(uint64 ByteCount, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateDimensions(int32 Width, int32 Height, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateUrlCount(int32 UrlCount, const DesignImageResources::FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateRemoteUrl(const FString& Url, bool bAllowUnsafeRemoteUrls, FString& OutError);
		/** Address bytes from the socket destination, not a separate DNS lookup. */
		static bool IsPublicNetworkAddress(TConstArrayView<uint8> Address);
		static bool LoadLocalFile(
			const FString& FilePath,
			const DesignImageResources::FImageResourceLimits& Limits,
			TArray<uint8>& OutData,
			FString& OutError);
		// Shared synchronization for one download batch. The operation budget itself is
		// borrowed from the synchronous DownloadUrls caller, while each receive callback
		// keeps this wrapper alive by value. DownloadUrls cancels/stops receive callbacks
		// before returning, so the caller-owned budget outlives every use of this pointer.
		struct FBulkDownloadBudget
		{
			FCriticalSection Mutex;
			DesignImageResources::FImageOperationBudget* Budget = nullptr;

			explicit FBulkDownloadBudget(DesignImageResources::FImageOperationBudget& InBudget)
				: Budget(&InBudget)
			{
			}
		};

		// Start all URLs, then synchronously tick HTTP until each has a result.
		// Streaming callbacks enforce shared and per-image limits; each request has its own deadline.
		// Callbacks share the budget mutex, while the budget itself is borrowed for this call.
		// Poll IsCancellationRequested alongside the interactive cancel button.
		static void DownloadUrls(
			const TArray<FString>& Urls,
			float TimeoutSeconds,
			bool bAllowUnsafeRemoteUrls,
			const DesignImageResources::FImageResourceLimits& Limits,
			const TSharedRef<FBulkDownloadBudget, ESPMode::ThreadSafe>& BulkBudget,
			TMap<FString, FUrlDownloadResult>& OutResults,
			const TFunction<bool()>& IsCancellationRequested = TFunction<bool()>());
		static bool DecodeToBGRA8(
			const TArray<uint8>& CompressedData,
			const DesignImageResources::FImageResourceLimits& Limits,
			int32& OutWidth,
			int32& OutHeight,
			TArray64<uint8>& OutRawData,
			FString& OutError,
			DesignImageResources::FImageOperationBudget* OperationBudget = nullptr,
			EDesignImageDiagnosticCategory* OutFailureCategory = nullptr);
	};
}
