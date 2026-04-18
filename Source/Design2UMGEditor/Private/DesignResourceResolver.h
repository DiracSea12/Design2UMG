// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UTexture2D;
struct FUMGPropertyBag;

namespace DesignResourceResolver
{
	// Apply font family string to FSlateFontInfo. Mirrors the existing inline logic exactly.
	void ApplyFontFamily(FSlateFontInfo& FontInfo, const FString& Value, bool bIsIconFontWidget);

	// Generate a UTexture2D from a material ref string via FProceduralShapeGenerator.
	UTexture2D* ResolveTextureFromMaterialRef(const FString& MaterialRef, const FUMGPropertyBag& Props, UObject* Outer);

	enum class ELoadMode : uint8
	{
		LoadOnly,        // LoadObject only (original PreBuildResourceNormalizer / WidgetResourceBinder path)
		LoadThenFind,    // LoadObject, fallback FindObject (original UMGPropertyApplierInternal path)
	};

	// Load a UTexture2D from an asset path.
	UTexture2D* LoadTextureFromPath(const FString& Path, ELoadMode Mode = ELoadMode::LoadOnly);
}
