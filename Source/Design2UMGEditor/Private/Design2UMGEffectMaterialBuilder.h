// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchemaTypedPayloads.h"

class UMaterialInterface;
struct FUMGWidgetNode;

namespace Design2UMGEffectMaterialBuilder
{
	/** Pixel-space Gaussian support sampled on a bounded quadrature grid. */
	struct FSubtreeShadowKernel
	{
		int32 Radius = 0;
		float Step = 1.0f;
		int32 TextureSampleCount = 1;
	};
	FSubtreeShadowKernel MakeSubtreeShadowKernel(float Sigma);
	UMaterialInterface* GetOrCreateForegroundBlurRetainerMaterial();
	UMaterialInterface* GetOrCreateGeometryMaskRetainerMaterial();
	UMaterialInterface* GetOrCreateGeometryMaskRetainerMaterial(
		EUMGGeometryMaskShape Shape,
		const FVector2D& Size,
		const FVector4& CornerRadii);
	UMaterialInterface* GetOrCreateInnerShadowRetainerMaterial();
	/** Persist subtree effects before modifying a generated WidgetTree. */
	bool PrepareSubtreeShadowMaterials(FUMGWidgetNode& Root, FString& OutError);

	/** Drop the root of every saved geometry mask instance; the /Game copy is reloaded on demand. */
	void ReleaseRootedGeometryMaskInstances();
}
