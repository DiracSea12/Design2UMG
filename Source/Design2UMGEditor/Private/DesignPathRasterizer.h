#pragma once

#include "CoreMinimal.h"

class UTexture2D;

class FDesignPathRasterizer
{
public:
	static UTexture2D* Rasterize(
		const FString& Geometry,
		int32 Width,
		int32 Height,
		const TOptional<FLinearColor>& FillColor,
		const FString& FillRule = TEXT("nonzero"),
		const TOptional<FLinearColor>& StrokeColor = TOptional<FLinearColor>(),
		float StrokeWidth = 0.0f,
		const FString& StrokeCap = TEXT("butt"),
		const FString& StrokeJoin = TEXT("miter"));
	static void FlushPendingTextureUpdates();
	static void ClearCache();
};
