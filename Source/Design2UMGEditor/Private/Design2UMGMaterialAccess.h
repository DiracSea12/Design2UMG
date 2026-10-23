// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;
class UMaterialInterface;
class UTexture;

namespace Design2UMGMaterialAccess
{
	void AddExpression(UMaterial& Material, UMaterialExpression& Expression);
	TConstArrayView<TObjectPtr<UMaterialExpression>> GetExpressions(const UMaterial& Material);
	void CacheShadersForRendering(UMaterialInterface& Material);
	void WaitForCompilation(UMaterialInterface& Material);
	// On failure, return final compile errors and the states around caching and completion.
	bool EnsureReadyForRendering(UMaterial& Material, FString& OutCompileErrors, FString& OutCompileStates);
	void RecompileForRendering(UMaterialInterface& Material);
	void GetUsedTextures(const UMaterialInterface& Material, TArray<UTexture*>& OutTextures);
}
