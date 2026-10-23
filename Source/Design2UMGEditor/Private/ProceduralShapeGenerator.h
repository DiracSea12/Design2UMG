// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UObject;
class UTexture2D;

namespace ProceduralShapeGeneratorAssets
{
	// Returns the raw pointers of the textures currently held rooted by this
	// generator. Used by the preview materialization service to scope the textures
	// it created during one attempt so a failed attempt releases only its own.
	TArray<UTexture2D*> GetRootedTextureSnapshot();
}

class FProceduralShapeGenerator
{
public:
	/** True only for material refs implemented by the CPU procedural generator. */
	static bool IsSupportedMaterialRef(const FString& MaterialRef);
	// Gradient raster windows use original paint-pixel coordinates without rescaling paint parameters.
	static UTexture2D* GenerateFromMaterialRef(const FUMGProceduralResourceRequest& Request, UObject* Outer = nullptr, FString* OutError = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr, const FIntRect* RasterRegion = nullptr);
	static UTexture2D* ApplyAffineCrop(UTexture2D* Source, float A, float B, float TX, float C, float D, float TY, int32 OutW, int32 OutH, FString* OutError = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr, const FString& FitMode = FString(), const FIntRect* RasterRegion = nullptr);
	static void ReleaseTransientTextures();
	static void ReleaseTransientTexture(UTexture2D* Texture);
	static void FlushPendingTextureUpdates();
	// Snapshot of the textures currently held rooted by this generator.
	static TArray<UTexture2D*> GetRootedTextureSnapshot();
};
