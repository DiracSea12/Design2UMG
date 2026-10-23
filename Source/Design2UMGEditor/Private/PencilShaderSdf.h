// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
class UMaterial;
class UTexture;
namespace PencilShaderSdf
{
constexpr int32 MaximumDimension = 1024;
constexpr int32 JumpMaterialCount = 11;
struct FPassMaterials
{
    UMaterial* Init = nullptr;
    UMaterial* Jump = nullptr;
    UMaterial* Distance = nullptr;
    UMaterial* Blur = nullptr;
    UMaterial* Gradient = nullptr;
};
// Native floating-point placeholder for material compilation, replaced on each real instance.
UTexture* SamplerDefault(FString& Error);
bool BuildMaterials(FPassMaterials& Out, FString& Error);
} // namespace PencilShaderSdf
