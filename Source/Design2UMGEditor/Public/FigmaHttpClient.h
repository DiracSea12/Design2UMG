// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Figma REST API HTTP client.
 *
 * Wraps Personal Access Token authentication and the common endpoints used by
 * the plugin. All requests are asynchronous and report through callbacks.
 */
class FFigmaHttpClient
{
public:
	FFigmaHttpClient() = default;

	void SetToken(const FString& InToken) { Token = InToken; }
	bool HasToken() const { return !Token.IsEmpty(); }

	using FOnComplete = TFunction<void(bool bSuccess, const FString& ResponseBody)>;

	/** GET /v1/files/:key - Fetch the full file payload. */
	void GetFile(const FString& FileKey, FOnComplete OnComplete);

	/** GET /v1/files/:key/images - Official "get file image fills" endpoint (imageRef -> URL map). */
	void GetFileImages(const FString& FileKey, FOnComplete OnComplete);

	/** GET /v1/images/:key?ids=...&format=png&scale=2 - Export nodes as images. */
	void ExportImages(const FString& FileKey, const TArray<FString>& NodeIds, FOnComplete OnComplete);

private:
	FString Token;

	void SendGet(const FString& Url, FOnComplete OnComplete);
};

