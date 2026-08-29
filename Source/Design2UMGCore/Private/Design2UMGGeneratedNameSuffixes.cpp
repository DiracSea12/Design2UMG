// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGGeneratedNameSuffixes.h"

namespace Design2UMGGeneratedNameSuffixes
{

const TCHAR* FxBlur() { return TEXT("_fxblur"); }
const TCHAR* GeometryMask() { return TEXT("_geometrymask"); }
const TCHAR* InnerShadow() { return TEXT("_innershadow"); }

const TCHAR* Surface() { return TEXT("_surface"); }
const TCHAR* Content() { return TEXT("_content"); }

const TCHAR* StrokeTop() { return TEXT("_stroke_top"); }
const TCHAR* StrokeRight() { return TEXT("_stroke_right"); }
const TCHAR* StrokeBottom() { return TEXT("_stroke_bottom"); }
const TCHAR* StrokeLeft() { return TEXT("_stroke_left"); }

const TCHAR* FillMarker() { return TEXT("_fill"); }
const TCHAR* FillImageSuffix() { return TEXT("_img"); }
const TCHAR* ShadowMarker() { return TEXT("_shadow"); }

FString Fill(const int32 Index)
{
	return FString::Printf(TEXT("%s%d"), FillMarker(), Index);
}

FString FillImage(const int32 Index)
{
	return FString::Printf(TEXT("%s%d%s"), FillMarker(), Index, FillImageSuffix());
}

FString Shadow(const int32 Index)
{
	return FString::Printf(TEXT("%s%d"), ShadowMarker(), Index);
}

}
