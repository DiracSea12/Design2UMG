// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UImage;
class UTexture2D;

class FWidgetResourceBinder
{
public:
	// Prefer material first, then fall back to texture; returns whether any resource was bound.
	static bool BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);
};
