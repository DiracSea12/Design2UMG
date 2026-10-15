// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;
class UMaterialInterface;

namespace Design2UMGMaterialAccess
{
	void AddExpression(UMaterial& Material, UMaterialExpression& Expression);
	TConstArrayView<TObjectPtr<UMaterialExpression>> GetExpressions(const UMaterial& Material);
	void CacheShadersForRendering(UMaterialInterface& Material);
	void WaitForCompilation(UMaterialInterface& Material);
	void RecompileForRendering(UMaterialInterface& Material);
}
