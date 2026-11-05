// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchemaTypedPayloads.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInterface;
class UWidget;
struct FUMGWidgetNode;
namespace DesignImageResources { class FImageOperationBudget; }

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
	UMaterialInterface* GetOrCreateInnerShadowRetainerMaterial(int32 BlendMode = 0);
	/** Resolve and persist typed effect parameters without WidgetTree-owned materials. */
	UMaterialInterface* GetOrCreateRetainerEffectMaterial(
		const FUMGEffectData& Effect, UMaterialInterface* ExistingMaterial = nullptr);
    /** Native brush paint; radii are pixels in top-left, top-right, bottom-right, bottom-left order. */
    bool PrepareSurfacePaintMaterial(FUMGWidgetNode& Producer,
        const TArray<FUMGSurfacePaint>& SourcePaints, const TArray<FUMGSurfacePaintTarget>& Targets,
        EUMGGeometryMaskShape Shape, const FVector2D& Size, const FVector4& CornerRadii, FString& OutError,
        DesignImageResources::FImageOperationBudget* Budget = nullptr);
	/** Prepare a child-content Retainer mask or a native Image shadow from the same endpoint payload. */
	bool PrepareSharedCaptureMaterial(FUMGWidgetNode& Owner, FString& OutError,
		DesignImageResources::FImageOperationBudget* Budget = nullptr);
	/** Persist the measured owner-local frame and paint pivot on the actual saved native material owner. */
	bool FinalizeSharedCaptureFrame(UWidget* Owner, const FVector4& CaptureRect, const FVector2D& Pivot, FString& OutError);
	/** Persist subtree effects before modifying a generated WidgetTree. */
	bool PrepareSubtreeShadowMaterials(FUMGWidgetNode& Root, FString& OutError);
	/** Resolve persisted effect dependencies before creating or replacing a WidgetTree. */
	bool PrepareRetainerEffectMaterials(FUMGWidgetNode& Root,
		TArray<TStrongObjectPtr<UMaterialInterface>>& Materials, FString& OutError,
		DesignImageResources::FImageOperationBudget* Budget = nullptr);

}
