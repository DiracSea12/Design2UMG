// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

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

	struct FResolveContext
	{
		FString ImportPath;
		FString FrameName;
		float TimeoutSeconds = 20.0f;
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
		static bool DownloadUrl(const FString& Url, float TimeoutSeconds, TArray<uint8>& OutData, FString& OutError);
		static bool DecodeToBGRA8(
			const TArray<uint8>& CompressedData,
			int32& OutWidth,
			int32& OutHeight,
			TArray64<uint8>& OutRawData,
			FString& OutError);
	};
}
