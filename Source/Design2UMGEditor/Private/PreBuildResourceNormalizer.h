// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "DesignImageResourceLimits.h"

class FPreBuildResourceNormalizer
{
public:
	// Validate each request's dimensions; charge cumulative bytes only at real allocations.
	static bool ValidateGeneratedTextureRequests(
		const FUMGWidgetNode& Root,
		FString& OutError);
	// Persist path, procedural and transformed-image textures in dependency order.
	static bool NormalizeGeneratedTextures(FUMGWidgetNode& Root, const FString& PackagePath,
		const FString& AssetPrefix, FString& OutError, DesignImageResources::FImageOperationBudget* Budget = nullptr);

	static bool NormalizePathTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr,
		DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static bool NormalizeProceduralTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static bool NormalizeImageTransformTextures(FUMGWidgetNode& Root, const FString& TextureImportPath, const FString& AssetPrefix, FString* OutError = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
};
