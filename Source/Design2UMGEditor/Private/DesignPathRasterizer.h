// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPathRasterDefaults.h"

class UTexture2D;

class FDesignPathRasterizer
{
public:
	static UTexture2D* Rasterize(
		const FString& Geometry,
		int32 Width,
		int32 Height,
		const TOptional<FLinearColor>& FillColor,
		const FString& FillRule = DesignPathRasterDefaults::FillRule,
		const TOptional<FLinearColor>& StrokeColor = TOptional<FLinearColor>(),
		float StrokeWidth = 0.0f,
		const FString& StrokeCap = DesignPathRasterDefaults::StrokeCap,
		const FString& StrokeJoin = DesignPathRasterDefaults::StrokeJoin);
	static void FlushPendingTextureUpdates();
	static void ClearCache();
};
