// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * Typed-first procedural resource request used by final editor consumers before
 * they adapt into the resolver's retained property-bag bridge.
 */
struct DESIGN2UMGCORE_API FUMGProceduralResourceRequest
{
	FString MaterialRef;
	TOptional<FString> MaterialParams;
	TOptional<float> WidthOverride;
	TOptional<float> HeightOverride;
	TOptional<float> ShadowBlur;
	TOptional<FLinearColor> BrushColor;

	bool HasNonEmptyMaterialRef() const
	{
		return !MaterialRef.IsEmpty();
	}

	FUMGPropertyBag BuildCompatibilityPropertyBag() const;
};

namespace Design2UMGWidgetNodeProceduralResourceAccess
{
	DESIGN2UMGCORE_API bool TryBuildProceduralResourceRequest(
		const FUMGWidgetNode& Node,
		FUMGProceduralResourceRequest& OutRequest);
}
