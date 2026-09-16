// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"

class UObject;
class UTexture2D;

class FProceduralShapeGenerator
{
public:
	static UTexture2D* GenerateFromMaterialRef(const FUMGProceduralResourceRequest& Request, UObject* Outer = nullptr, FString* OutError = nullptr);
	static UTexture2D* ApplyAffineCrop(UTexture2D* Source, float A, float B, float TX, float C, float D, float TY, int32 OutW, int32 OutH, FString* OutError = nullptr);
	static void ReleaseTransientTextures();
	static void ReleaseTransientTexture(UTexture2D* Texture);
	static void FlushPendingTextureUpdates();
};
