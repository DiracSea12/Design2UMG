// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "Design2UMGFontAccess.h"
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

		const float PointSize = SourceFontSizePx * 72.0f / Design2UMGFontAccess::RenderDPI;
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

	/** Italic is declared as part of the source fontStyle string ("SemiBold Italic", "Italic", ...). */
	inline bool IsItalicFontStyle(const FString& FontStyle)
	{
		return FontStyle.Contains(TEXT("italic"));
	}

	inline FResolvedFontSelection Resolve(
		const FString& FontFamily,
		const FString& FontPostScriptName,
		const FString& FontWeight,
		const FString& FontStyle = FString(),
		const FDesign2UMGFontMappings* FontMappings = nullptr)
	{
		FResolvedFontSelection Result;
		Result.bRequiresResolvedFontAsset = FFontResolver::HasInvalidProjectFontAsset(FontFamily);

		const bool bExplicitMapping = FFontResolver::HasExplicitFontMapping(FontFamily, FontPostScriptName, FontMappings);
		const FString EffectiveWeight = FontWeight.IsEmpty() ? TEXT("normal") : FontWeight;
		const FString CandidateAssetPath = FFontResolver::FindFontAssetPath(FontFamily, FontPostScriptName, FontMappings);
		const FString ItalicTypefaceName = (IsItalicFontStyle(FontStyle) && !FontWeight.IsEmpty())
			? FFontResolver::FindItalicTypefaceForAssetPath(CandidateAssetPath, EffectiveWeight, FontFamily)
			: FString();
		if (bExplicitMapping)
		{
			if (FontWeight.IsEmpty())
			{
				Result.FontAssetPath = CandidateAssetPath;
				return Result;
			}

			if (!ItalicTypefaceName.IsEmpty())
			{
				Result.FontAssetPath = CandidateAssetPath;
				Result.TypefaceName = ItalicTypefaceName;
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
		if (!ItalicTypefaceName.IsEmpty())
		{
			Result.TypefaceName = ItalicTypefaceName;
			return Result;
		}
		Result.TypefaceName = FFontResolver::FindBestTypefaceForAssetPath(Result.FontAssetPath, EffectiveWeight);
		if (Result.TypefaceName.IsEmpty())
		{
			Result.TypefaceName = FFontResolver::FindBestTypefaceForAssetPath(FFontResolver::FindFontAssetPath(FontFamily, FString(), FontMappings), EffectiveWeight);
		}
		if (Result.TypefaceName.IsEmpty())
		{
			Result.TypefaceName = DesignTypeMapper::MapFontWeight(EffectiveWeight).ToString();
		}
		return Result;
	}
}
