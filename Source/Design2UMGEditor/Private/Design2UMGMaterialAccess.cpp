// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGMaterialAccess.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "Misc/EngineVersionComparison.h"

namespace Design2UMGMaterialAccess
{
	void GetUsedTextures(const UMaterialInterface& Material, TArray<UTexture*>& OutTextures)
	{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		Material.GetUsedTextures(OutTextures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);
#else
		Material.GetUsedTextures(OutTextures);
#endif
	}

	void AddExpression(UMaterial& Material, UMaterialExpression& Expression)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		Material.Expressions.AddUnique(&Expression);
#else
		Material.GetExpressionCollection().AddExpression(&Expression);
#endif
	}

	TConstArrayView<TObjectPtr<UMaterialExpression>> GetExpressions(const UMaterial& Material)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return Material.Expressions;
#else
		return Material.GetExpressions();
#endif
	}

	void WaitForCompilation(UMaterialInterface& Material)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		for (int32 FeatureLevelIndex = 0; FeatureLevelIndex < ERHIFeatureLevel::Num; ++FeatureLevelIndex)
		{
			const auto FeatureLevel = static_cast<ERHIFeatureLevel::Type>(FeatureLevelIndex);
			if (FMaterialResource* Resource = Material.GetMaterialResource(FeatureLevel))
			{
				Resource->FinishCompilation();
			}
		}
#else
		Material.EnsureIsComplete();
#endif
	}

	void CacheShadersForRendering(UMaterialInterface& Material)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		// UE 5.0's exported entry populates rendering resources without invalidating their shader keys.
		Material.ForceRecompileForRendering();
		WaitForCompilation(Material);
#else
		Material.CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
#endif
	}

	void RecompileForRendering(UMaterialInterface& Material)
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		Material.ForceRecompileForRendering();
#else
		Material.ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
#endif
#if !UE_VERSION_OLDER_THAN(5, 1, 0)
		CacheShadersForRendering(Material);
#endif
		WaitForCompilation(Material);
	}
}
