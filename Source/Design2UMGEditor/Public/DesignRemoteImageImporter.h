// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UTexture2D;

/**
 * Resolve TexturePath fields in generated UMG data.
 * - /Game/... and /Engine/... paths are preserved
 * - http/https URLs are downloaded and imported as Texture2D assets
 */
class FDesignRemoteImageImporter
{
public:
	/**
	 * Resolve remote images and rewrite TexturePath to UE asset object paths.
	 * Returns false when one or more URL images failed to import.
	 */
	static bool ResolveRemoteImages(FUMGWidgetData& InOutData, FString* OutReport = nullptr);

	/**
	 * 收集 Widget 树中所有远程 URL 的 TexturePath，下载为 RF_Transient 内存纹理。
	 * 用于预览场景，不创建持久化资产。
	 * @param RootNode Widget 树根节点
	 * @param OutTextureMap 输出：TexturePath(URL) → UTexture2D*
	 */
	static void ResolveTransientTextures(const FUMGWidgetNode& RootNode, TMap<FString, UTexture2D*>& OutTextureMap);

	/** 释放 ResolveTransientTextures 创建的临时纹理的 Root 引用 */
	static void ReleaseTransientTextures(TMap<FString, UTexture2D*>& InOutTextureMap);
};

