// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesignNode;
struct FPenFont;
class UFont;

struct DESIGN2UMGEDITOR_API FFontResolver
{
	/** Scan document for all used font families, compare with project assets, return missing list */
	static TArray<FString> FindMissingFonts(const FDesignDocument& Document);

	/** Download font from URL and import as UFont asset. Returns asset path or empty on failure. */
	static FString ResolveFont(const FString& FontName, const FString& Url);

	/** Auto-import all fonts from document if bAutoImportFonts is enabled */
	static void AutoImportFonts(const FDesignDocument& Document);

	/** Look up cached asset path for a font family name. Returns empty if not found. */
	static FString FindFontAssetPath(const FString& FontFamily);

	/** Clear the font asset path cache */
	static void ClearCache();

private:
	static TMap<FString, FString> FontAssetCache;
	static void CollectFontFamilies(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutFamilies);
	static TSet<FString> ScanProjectFontAssets();
	static bool DownloadToFile(const FString& Url, const FString& LocalPath);
};
