// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class FPreBuildResourceNormalizer
{
public:
	static void NormalizePathTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix);
	static void NormalizeProceduralTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix);
};
