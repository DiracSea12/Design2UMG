// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
struct FUMGWidgetNode;
class UMaterial;
namespace DesignImageResources { class FImageOperationBudget; }
namespace PencilShaderMaterials
{
FString BlendLibrary();
bool BlendModeIndex(const FString& Name, int32& OutMode, FString& Error);
bool PrepareBackgroundMaterials(UMaterial*& Composite, UMaterial*& Consumer, UMaterial*& Identity, UMaterial*& Opacity, FString& Error);
bool PrepareObjectCapture(UMaterial*& TransparentOutput, UMaterial*& Copy, FString& Error);
bool PrepareGeometryMasks(FUMGWidgetNode& Root,FString& Error);
bool Prepare(FUMGWidgetNode& Root,FString& Error,DesignImageResources::FImageOperationBudget& Budget);
}
