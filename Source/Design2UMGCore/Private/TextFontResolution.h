// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "Fonts/SlateFontInfo.h"

namespace TextFontResolution
{
	inline TOptional<float> ResolveSourceFontSizeForUMG(const float SourceFontSizePx)
	{
		if (!FMath::IsFinite(SourceFontSizePx) || SourceFontSizePx <= 0.0f)
		{
			return {};
		}

		const float PointSize = SourceFontSizePx * 72.0f / static_cast<float>(FontConstants::RenderDPI);
		if (!FMath::IsFinite(PointSize)
			|| PointSize <= 0.0f
			|| PointSize > static_cast<float>(TNumericLimits<uint16>::Max()))
		{
			return {};
		}

		return PointSize;
	}

	struct FResolvedFontSelection
	{
		FString FontAssetPath;
		FString TypefaceName;
		bool bRequiresResolvedFontAsset = false;
	};

	inline FResolvedFontSelection Resolve(
		const FString& FontFamily,
		const FString& FontPostScriptName,
		const FString& FontWeight)
	{
		FResolvedFontSelection Result;
		Result.bRequiresResolvedFontAsset = FFontResolver::HasInvalidProjectFontAsset(FontFamily);

		const bool bExplicitMapping = FFontResolver::HasExplicitFontMapping(FontFamily, FontPostScriptName);
		const FString EffectiveWeight = FontWeight.IsEmpty() ? TEXT("normal") : FontWeight;
		const FString CandidateAssetPath = FFontResolver::FindFontAssetPath(FontFamily, FontPostScriptName);
		if (bExplicitMapping)
		{
			if (FontWeight.IsEmpty())
			{
				Result.FontAssetPath = CandidateAssetPath;
				return Result;
			}

			const FString ExactTypefaceName =
				FFontResolver::FindExactTypefaceForAssetPath(CandidateAssetPath, EffectiveWeight);
			if (!ExactTypefaceName.IsEmpty())
			{
				Result.FontAssetPath = CandidateAssetPath;
				Result.TypefaceName = ExactTypefaceName;
			}
			return Result;
		}

		Result.FontAssetPath = CandidateAssetPath;
		if (FontWeight.IsEmpty())
		{
			return Result;
		}
		Result.TypefaceName = FFontResolver::FindBestTypefaceForAssetPath(Result.FontAssetPath, EffectiveWeight);
		if (Result.TypefaceName.IsEmpty())
		{
			Result.TypefaceName = FFontResolver::FindBestTypeface(FontFamily, EffectiveWeight);
		}
		if (Result.TypefaceName.IsEmpty())
		{
			Result.TypefaceName = DesignTypeMapper::MapFontWeight(EffectiveWeight).ToString();
		}
		return Result;
	}
}
