// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * Typed procedural resource request used by final editor consumers.
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

};

namespace Design2UMGWidgetNodeProceduralResourceAccess
{
	DESIGN2UMGCORE_API bool TryBuildProceduralResourceRequest(
		const FUMGWidgetNode& Node,
		FUMGProceduralResourceRequest& OutRequest);
}
