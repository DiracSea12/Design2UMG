// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "Design2UMGFontAccess.h"
#include "DesignSchemaFoundation.h"
#include "FontResolver.h"

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

	inline FResolvedFontSelection Resolve(
		const FString& FontFamily, const FString& FontPostScriptName, const FString& FontWeight,
		const FString& FontStyle = FString(), const FDesign2UMGFontMappings* FontMappings = nullptr,
		const FDesignPreparedFont* PreparedFont = nullptr)
	{
		auto Result = FFontResolver::ResolveFontSelection(FontFamily, FontPostScriptName, FontWeight, FontStyle, FontMappings, PreparedFont);
		// Ref expansion and mapping snapshots may select a different face than preparation did.
		if (PreparedFont && !PreparedFont->AssetPath.IsEmpty()
			&& Result.FontAssetPath == PreparedFont->SourceAssetPath
			&& FName(*Result.TypefaceName) == PreparedFont->TypefaceName)
		{
			Result.FontAssetPath = PreparedFont->AssetPath;
			Result.bRequiresResolvedFontAsset = true;
		}
		return Result;
	}
}
