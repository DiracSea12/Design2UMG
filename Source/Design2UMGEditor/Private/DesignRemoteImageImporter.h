// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImageDiagnostics.h"
#include "UMGSchema.h"

class UTexture2D;

/**
 * Resolve TexturePath fields in generated UMG data.
 * - /Game/... and /Engine/... paths are preserved
 * - http/https URLs are downloaded and imported as Texture2D assets
 */
struct FDesignImageImportSettingsSnapshot;

class DESIGN2UMGEDITOR_API FDesignRemoteImageImporter
{
public:
	/**
	 * True when the string starts with an http:// or https:// scheme prefix
	 * (case-insensitive). Single shared predicate for the importer, pipeline,
	 * and diagnostic-report URL checks so the scheme set cannot drift between them.
	 */
	static bool IsHttpUrl(const FString& Value);

	/**
	 * Resolve remote images and rewrite TexturePath to UE asset object paths.
	 * Returns structured outcomes for all attempted image resolutions.
	 */
	static FDesignImageImportResult ResolveRemoteImages(FUMGWidgetData& InOutData,
		const FDesignImageImportSettingsSnapshot& Settings);

	/**
	 * Collect all remote TexturePath URLs in the widget tree and download them
	 * as RF_Transient textures for preview-only use.
	 * @param RootNode Root widget node to scan.
	 * @param OutTextureMap Output map: TexturePath(URL) -> UTexture2D*.
	 */
	static void ResolveTransientTextures(const FUMGWidgetNode& RootNode, TMap<FString, UTexture2D*>& OutTextureMap);

	/** Release Root references held by temporary textures created for preview. */
	static void ReleaseTransientTextures(TMap<FString, UTexture2D*>& InOutTextureMap);
};

