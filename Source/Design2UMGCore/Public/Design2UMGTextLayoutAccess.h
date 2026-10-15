// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/TextWidgetTypes.h"

class UTextLayoutWidget;

namespace Design2UMGTextLayoutAccess
{
	struct FUpdate
	{
		TOptional<float> LineHeightPercentage;
		TOptional<bool> ApplyLineHeightToBottomLine;
		TOptional<bool> AutoWrapText;
		TOptional<float> WrapTextAt;
		TOptional<FMargin> Margin;
		TOptional<FShapedTextOptions> ShapedTextOptions;
	};

	// Applies one coherent batch so engines without public setters synchronize
	// the underlying Slate widget at most once.
	DESIGN2UMGCORE_API void Apply(UTextLayoutWidget& TextWidget, const FUpdate& Update);
}
