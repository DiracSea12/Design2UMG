// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
class UTexture2D;
struct FDesignShaderData;
namespace DesignImageResources { class FImageOperationBudget; }
namespace PencilShaderTextures
{
// Effective authored image values, using the same override/default semantics as material binding.
TMap<FString, FString> ImageUniforms(const FDesignShaderData& Shader);

// Preserve the shared UI texture; shader sampler settings use a separately persisted native asset.
UTexture2D* SamplerImage(UTexture2D* Source,bool bMipmap,DesignImageResources::FImageOperationBudget& Budget,FString& Error);
}
