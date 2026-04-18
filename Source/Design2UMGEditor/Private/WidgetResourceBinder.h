// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UImage;
class UTexture2D;

class FWidgetResourceBinder
{
public:
	// material 优先，失败后 texture fallback。返回是否成功绑定任意资源。
	static bool BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);
};
