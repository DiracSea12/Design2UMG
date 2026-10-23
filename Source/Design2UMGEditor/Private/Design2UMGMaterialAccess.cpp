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

	bool EnsureReadyForRendering(UMaterial& Material, FString& OutCompileErrors, FString& OutCompileStates)
	{
		const auto GetResource = [&Material]() -> const FMaterialResource*
		{
			return Material.GetMaterialResource(
#if UE_VERSION_OLDER_THAN(5, 7, 0)
				GMaxRHIFeatureLevel
#else
				GMaxRHIShaderPlatform
#endif
			);
		};
		const auto IsReady = [](const FMaterialResource* Resource)
		{
			return Resource && Resource->GetCompileErrors().IsEmpty() && Resource->GetGameThreadShaderMap() &&
				Resource->GetGameThreadShaderMap()->IsValidForRendering();
		};
		const auto DescribeState = [](const FMaterialResource* Resource)
		{
			const FString Errors = Resource ? FString::Join(Resource->GetCompileErrors(), TEXT("; ")) : FString();
			return FString::Printf(TEXT("resource=%d shaderMap=%d caching=%d compileErrors=%s"),
				Resource != nullptr, Resource && Resource->GetGameThreadShaderMap() != nullptr,
				Resource && Resource->IsCachingShaders(), Errors.IsEmpty() ? TEXT("<none>") : *Errors);
		};
		// Re-entering CacheShaders can replace a reused material's already renderable map.
		WaitForCompilation(Material);
		if (IsReady(GetResource())) return true;
		const FString BeforeCache = DescribeState(GetResource());
		CacheShadersForRendering(Material);
		const FString AfterCache = DescribeState(GetResource());
		WaitForCompilation(Material);
		const FMaterialResource* Resource = GetResource();
		if (IsReady(Resource)) return true;
		OutCompileErrors = Resource && !Resource->GetCompileErrors().IsEmpty()
			? FString::Join(Resource->GetCompileErrors(), TEXT("; ")) : TEXT("no compile errors");
		OutCompileStates = FString::Printf(TEXT("beforeCache={%s} afterCache={%s} afterEnsure={%s}"),
			*BeforeCache, *AfterCache, *DescribeState(Resource));
		return false;
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
