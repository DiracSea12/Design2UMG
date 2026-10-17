// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchema.h"

class UWidget;
class UTexture2D;

namespace DesignImageResources { class FImageOperationBudget; }

namespace UMGPropertyApplierInternal
{
	void ApplyCommonProperties(UWidget* Widget, const FUMGWidgetNode& Node);
	void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node, DesignImageResources::FImageOperationBudget* Budget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);
}
