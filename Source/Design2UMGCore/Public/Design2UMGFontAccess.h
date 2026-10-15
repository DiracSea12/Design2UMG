// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "AssetRegistry/AssetData.h"
#include "Engine/Font.h"
#include "Misc/EngineVersionComparison.h"

namespace Design2UMGFontAccess
{
	constexpr float RenderDPI = 96.0f;

	inline auto GetAssetClassKey()
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return UFont::StaticClass()->GetFName();
#else
		return UFont::StaticClass()->GetClassPathName();
#endif
	}

	inline bool IsFontAsset(const FAssetData& Asset)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return Asset.AssetClass == GetAssetClassKey();
#else
		return Asset.AssetClassPath == GetAssetClassKey();
#endif
	}

#if UE_VERSION_OLDER_THAN(5, 3, 0)
	using FNativeFontSize = int32;
#else
	using FNativeFontSize = float;
#endif

	inline FNativeFontSize ToNativeFontSize(const float FontSize)
	{
		// UE 5.1/5.2 stored Size as int32, so preserve its existing truncation-toward-zero behavior.
		return static_cast<FNativeFontSize>(FontSize);
	}

	inline FCompositeFont& GetMutableCompositeFont(UFont& Font)
	{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		return Font.CompositeFont;
#else
		return Font.GetMutableInternalCompositeFont();
#endif
	}
}
