// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchemaTypedPayloads.h"

class UMaterialInterface;

namespace Design2UMGEffectMaterialBuilder
{
	UMaterialInterface* GetOrCreateForegroundBlurRetainerMaterial();
	UMaterialInterface* GetOrCreateGeometryMaskRetainerMaterial();
	UMaterialInterface* GetOrCreateGeometryMaskRetainerMaterial(
		EUMGGeometryMaskShape Shape,
		const FVector2D& Size,
		const FVector4& CornerRadii);
	UMaterialInterface* GetOrCreateInnerShadowRetainerMaterial();

	/** Drop the root of every saved geometry mask instance; the /Game copy is reloaded on demand. */
	void ReleaseRootedGeometryMaskInstances();
}
