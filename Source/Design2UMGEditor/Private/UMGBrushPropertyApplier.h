// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "UMGSchema.h"

class UBackgroundBlur;
class URetainerBox;
class UWidget;

namespace UMGBrushPropertyApplier
{
	void ApplyImageProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyBorderProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyRetainerBoxProps(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyTypedRetainerBoxProps(URetainerBox* RB, const FUMGEffectData& Effect);
	void ApplyTypedBackgroundBlurProps(UBackgroundBlur* Blur, const FUMGEffectData& Effect);
}
