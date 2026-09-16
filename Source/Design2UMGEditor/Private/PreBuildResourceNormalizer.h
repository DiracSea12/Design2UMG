// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class FPreBuildResourceNormalizer
{
public:
	static bool ValidatePathTextureBudget(const FUMGWidgetNode& Root, FString& OutError);
	static bool NormalizePathTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr);
	static bool NormalizeProceduralTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr);
	static bool NormalizeImageTransformTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr);
#if WITH_DEV_AUTOMATION_TESTS
	static void ArmTextureSaveFailureOnce();
#endif
};
