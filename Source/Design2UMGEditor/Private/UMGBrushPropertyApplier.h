// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UBackgroundBlur;
class URetainerBox;
class UWidget;
class UTexture2D;

namespace UMGBrushPropertyApplier
{
	void ApplyImageProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyBorderProps(UWidget* Widget, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);
	void ApplyRetainerBoxProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyTypedRetainerBoxProps(URetainerBox* RB, const FUMGEffectData& Effect);
	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect);
}
