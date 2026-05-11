// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

class UBorder;
class UBackgroundBlur;
class URetainerBox;
class UWidget;

namespace UMGBrushPropertyApplier
{
	void ApplyImageProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyBorderProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyTypedBorderBrush(UBorder* Border, const FUMGVisualBrushData& BrushData);
	void ApplyRetainerBoxProps(UWidget* Widget, const FUMGPropertyBag& Properties);
	void ApplyTypedRetainerBoxProps(URetainerBox* RB, const FUMGEffectData& Effect);
	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect);
}
