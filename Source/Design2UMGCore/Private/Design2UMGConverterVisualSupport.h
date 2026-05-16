// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "UMGSchema.h"
#include "VisualMapper.h"

namespace Design2UMGConverterVisualSupport
{
	struct FConversionVisualSettingsPayload
	{
		UDesign2UMGSettings::FResolvedPlaceholderHatchStyle PlaceholderHatchStyle;
		FVisualMapperOptions VisualMapperOptions;
	};

	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node);

	void ApplyBaseVisualProperties(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node);

	void ApplyRenderFallbackVisuals(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node);

	bool TryApplyPlaceholderSurfaceTint(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FConversionVisualSettingsPayload& VisualSettings);

	bool ShouldWrapNodeWithBorder(
		const TSharedPtr<FDesignNode>& Node,
		bool bPlaceholderUsesDirectImage);

	FUMGWidgetNode WrapWithBorder(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node);

	FUMGWidgetNode ApplyVisualEffectWrappers(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& VisualMapperOptions);
}
