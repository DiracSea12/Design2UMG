// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FPenDocument;
class FFigmaHttpClient;

/**
 * Figma 图片导出器
 *
 * 收集 FPenDocument 中的 imageRef，调用 Figma images API 获取实际 URL，
 * 然后回填到节点树中。后续由 PenRemoteImageImporter 下载并导入 UE 资产。
 */
class FFigmaImageExporter
{
public:
	using FOnComplete = TFunction<void(bool bSuccess)>;

	/**
	 * 异步解析文档中所有 imageRef 为实际 URL
	 * @param Client 已配置 Token 的 HTTP 客户端
	 * @param FileKey Figma 文件 key
	 * @param Document 待处理的文档（共享所有权，异步安全）
	 * @param OnComplete 完成回调
	 */
	static void ResolveImageRefs(
		FFigmaHttpClient& Client,
		const FString& FileKey,
		TSharedRef<FPenDocument> Document,
		FOnComplete OnComplete);

	/**
	 * 将无法原生渲染的向量节点（VECTOR/BOOLEAN_OPERATION）导出为 PNG
	 *
	 * 调用 Figma GET /v1/images/:key?ids=...&format=png 获取渲染图，
	 * 然后将节点类型从 Path 改为 Rectangle + Image Fill。
	 */
	static void ExportVectorNodes(
		FFigmaHttpClient& Client,
		const FString& FileKey,
		TSharedRef<FPenDocument> Document,
		FOnComplete OnComplete);
};
