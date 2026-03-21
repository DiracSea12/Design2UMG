// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesignNode;
struct FDesignFont;
class UFont;

struct DESIGN2UMGCORE_API FFontResolver
{
	/** Scan document for all used font families, compare with project assets, return missing list */
	static TArray<FString> FindMissingFonts(const FDesignDocument& Document);

	/** Download font from URL and import as UFont asset. Returns asset path or empty on failure. */
	static FString ResolveFont(const FString& FontName, const FString& Url);

	/** Auto-import all fonts from document if bAutoImportFonts is enabled */
	static void AutoImportFonts(const FDesignDocument& Document);

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

private:
	static int32 WeightFromTypefaceName(const FString& Name);
	static FString WeightNameFromNumeric(int32 Weight);
	static FString WeightNameFromFile(const FString& FilePath, const FString& FamilyName);
	static TArray<FString> FindSiblingFontFiles(const FString& FilePath, const FString& FamilyName);
	static bool QueryKnownWebFontTypefaces(const FString& FontFamily, TMap<FString, FString>& OutTypefaceUrls);
	static bool EnsureKnownWebFontTypefaces(const FString& FontFamily, UFont* Font, const FString& FontObjectPath);
	static TMap<FString, FString> FontAssetCache;
	static void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);
	static TSet<FString> ScanProjectFontAssets();
	static bool DownloadToFile(const FString& Url, const FString& LocalPath);
};


