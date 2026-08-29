// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

// Single source for path rasterization defaults shared by brush application
// (UMGBrushPropertyApplier), pre-build texture materialization (PreBuildResourceNormalizer),
// and the rasterizer entry-point defaults (DesignPathRasterizer). Callers keep their own
// fallback semantics; only the literal values live here.
namespace DesignPathRasterDefaults
{
	inline constexpr TCHAR FillRule[] = TEXT("nonzero");
	inline constexpr TCHAR StrokeCap[] = TEXT("butt");
	inline constexpr TCHAR StrokeJoin[] = TEXT("miter");
	inline constexpr int32 DefaultPixelSize = 64;
	inline constexpr int32 Supersample = 2;
}
