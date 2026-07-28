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

		uint64 MaxCompressedBytes = DefaultMaxCompressedBytes;
		int32 MaxDimension = DefaultMaxDimension;
		int64 MaxPixels = DefaultMaxPixels;
		int32 MaxUrlsPerOperation = DefaultMaxUrlsPerOperation;
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

	struct FResolveContext
	{
		FString ImportPath;
		FString FrameName;
		float TimeoutSeconds = 20.0f;
		FImageResourceLimits ImageLimits;
		FResolveReport Report;
		TMap<FString, FString> UrlToAssetPath;
		TMap<FString, FString> ContentHashToAsset;
	};

	class FImportedTextureIndex
	{
	public:
		static void Remember(const FString& ImportPath, const FString& AssetName, const FString& ObjectPath);
		static void Forget(const FString& ImportPath, const FString& AssetName);
		static bool TryGetPersistedPath(const FString& ImportPath, const FString& AssetName, FString& OutObjectPath);
	};

	class FImagePayloadIO
	{
	public:
		static bool ValidateCompressedByteCount(uint64 ByteCount, const FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateDimensions(int32 Width, int32 Height, const FImageResourceLimits& Limits, FString& OutError);
		static bool ValidateUrlCount(int32 UrlCount, const FImageResourceLimits& Limits, FString& OutError);
		static bool DownloadUrl(
			const FString& Url,
			float TimeoutSeconds,
			const FImageResourceLimits& Limits,
			TArray<uint8>& OutData,
			FString& OutError);
		static bool DecodeToBGRA8(
			const TArray<uint8>& CompressedData,
			const FImageResourceLimits& Limits,
			int32& OutWidth,
			int32& OutHeight,
			TArray64<uint8>& OutRawData,
			FString& OutError);
	};
}
