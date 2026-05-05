// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FDesignDocument;
struct FDesignNode;
struct FDesignFont;
class UFont;

struct DESIGN2UMGCORE_API FFontResolver
{
	/** Scan document for all used font families, compare with project assets, return missing list */
	static TArray<FString> FindMissingFonts(const FDesignDocument& Document);

	/** Look up cached asset path for a font family name. Returns empty if not found. */
	static FString FindFontAssetPath(const FString& FontFamily);

	/** Resolve a known icon family/name pair to a concrete glyph string when the icon set ships codepoints. */
	static FString FindKnownIconGlyph(const FString& FontFamily, const FString& IconName);

	/** Resolve a rendered glyph back to its semantic icon name when the icon set ships codepoints. */
	static FString FindKnownIconNameByGlyph(const FString& FontFamily, const FString& Glyph);

	/** Find best matching typeface name for a given weight. Returns empty if font not loaded. */
	static FString FindBestTypeface(const FString& FontFamily, const FString& RequestedWeight);

	/** Parse a weight string ("bold", "700", etc.) to numeric value (100-900). */
	static int32 ParseWeightValue(const FString& Weight);

	/** Clear the font asset path cache */
	static void ClearCache();

	/** Seed a known font-family to asset-path mapping for the current deterministic import. */
	static void RegisterFontAssetPath(const FString& FontFamily, const FString& ObjectPath);

	/** Register glyph/name lookups for a known icon family. */
	static void RegisterKnownIconGlyphs(
		const FString& FontFamily,
		const TMap<FString, FString>& Glyphs,
		const TMap<FString, FString>& IconNames);

	/** Register an editor-side callback that can attempt to import/resolve a missing font family. */
	static void RegisterMissingFontResolver(TFunction<FString(const FString&)> InResolver);

	/** Register an editor-side callback that can lazily prime icon glyph/name lookups for a family. */
	static void RegisterKnownIconGlyphPrimer(TFunction<void(const FString&)> InPrimer);

	/** Remove any editor-side resolver callbacks. */
	static void ResetExternalResolvers();

private:
	static int32 WeightFromTypefaceName(const FString& Name);
	static FString WeightNameFromNumeric(int32 Weight);
	static TMap<FString, FString> FontAssetCache;
	static void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);
	static TSet<FString> ScanProjectFontAssets();
};


