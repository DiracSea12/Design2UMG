#pragma once

#include "CoreMinimal.h"

class UTexture2D;

class FDesignPathRasterizer
{
public:
	static UTexture2D* Rasterize(const FString& Geometry, int32 Width, int32 Height, FLinearColor FillColor, const FString& FillRule = TEXT("nonzero"));
	static void FlushPendingTextureUpdates();
	static void ClearCache();
};
