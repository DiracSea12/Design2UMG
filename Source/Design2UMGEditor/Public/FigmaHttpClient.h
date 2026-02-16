// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Figma REST API HTTP 客户端
 *
 * 封装 Personal Access Token 认证和常用端点调用。
 * 所有请求均为异步，通过回调返回结果。
 */
class FFigmaHttpClient
{
public:
	FFigmaHttpClient() = default;

	void SetToken(const FString& InToken) { Token = InToken; }
	bool HasToken() const { return !Token.IsEmpty(); }

	using FOnComplete = TFunction<void(bool bSuccess, const FString& ResponseBody)>;

	/** GET /v1/files/:key — 获取完整文件 */
	void GetFile(const FString& FileKey, FOnComplete OnComplete);

	/** GET /v1/files/:key/images — 获取文件中所有图片的 imageRef→URL 映射 */
	void GetFileImages(const FString& FileKey, FOnComplete OnComplete);

	/** GET /v1/images/:key?ids=...&format=png&scale=2 — 导出节点为图片 */
	void ExportImages(const FString& FileKey, const TArray<FString>& NodeIds, FOnComplete OnComplete);

private:
	FString Token;

	void SendGet(const FString& Url, FOnComplete OnComplete);
};
