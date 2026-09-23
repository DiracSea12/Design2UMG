// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPtr.h"

struct FDesignDocument;
struct FDesignNode;
struct FDesignFont;
class UFont;

using FDesign2UMGFontMappings = TMap<FString, TSoftObjectPtr<UFont>>;

struct DESIGN2UMGCORE_API FFontResolutionDiagnostic
{
	FString DeclarationKey;
	FString DeclaredFamily;
	FString DeclaredPostScriptName;
	FString DeclaredWeight;
	FString DeclaredStyle;
	FString ResolvedAssetPath;
	FString FallbackAssetPath;
	FString FallbackToken;
	FString UnresolvedToken;
	FString ResolvedTypeface;
	FString ResolutionState;
	FString DeclarationSourceKind;
	FString ResolutionSourceKind;
	FString FallbackState;
	FString MatchReason;
	FString Severity;
	TArray<FString> IssueCodes;
};

/**
 * Game-thread API: lookups load UFont assets, query the registry and read editor settings.
 * Cache locks protect internal consistency; they do not enable off-thread asset access.
 */
struct DESIGN2UMGCORE_API FFontResolver
{
	/** Scan document for all used font families, compare with project assets, return missing list */
	static TArray<FString> FindMissingFonts(const FDesignDocument& Document);

	/** Look up an explicit or exact /Game UFont asset path for a font family name. Returns empty if not found or ambiguous. */
	static FString FindFontAssetPath(const FString& FontFamily);

	/** Look up PostScript/family mappings before exact /Game matching. Supplying a mapping snapshot
	 * avoids reading the settings CDO; nullptr is the standalone editor lookup API. */
	static FString FindFontAssetPath(const FString& FontFamily, const FString& PostScriptName,
		const FDesign2UMGFontMappings* ExplicitMappings = nullptr);
	/** Stable, collision-resistant package name shared by automatic import and family lookup. */
	static FString MakeImportedFamilyPackageName(const FString& FontFamily);

	/** Look up configured project default font before engine default fallback. */
	static FString FindDefaultFontAssetPath();

	/** Resolve a known icon family/name pair to a concrete glyph string when the icon set ships codepoints. */
	static FString FindKnownIconGlyph(const FString& FontFamily, const FString& IconName);

	/** Normalize an icon name for resolver lookups: lower-case, separators, and the static alias table. */
	static FString NormalizeIconNameKey(const FString& IconName);

	/** Resolve a rendered glyph back to its semantic icon name when the icon set ships codepoints. */
	static FString FindKnownIconNameByGlyph(const FString& FontFamily, const FString& Glyph);

	/** Find best matching typeface name for a given weight. Returns empty if font not loaded. */
	static FString FindBestTypeface(const FString& FontFamily, const FString& RequestedWeight);

	/** Find best matching typeface name inside an already resolved UFont asset. */
	static FString FindBestTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight);

	/** Find an exact weight typeface inside an already resolved UFont asset. Returns empty when missing. */
	static FString FindExactTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight);

	/** Find an exact "<Weight> Italic" typeface inside an already resolved UFont asset. Returns empty when
	  missing, after emitting one deduplicated Warning per (asset, typeface) so the caller falls back to roman. */
	static FString FindItalicTypefaceForAssetPath(const FString& FontAssetPath, const FString& RequestedWeight, const FString& FontFamily);

	/** Return true when the declaration is covered by an explicit settings mapping, even if the mapped asset is invalid. */
	static bool HasExplicitFontMapping(const FString& FontFamily, const FString& PostScriptName = FString(),
		const FDesign2UMGFontMappings* ExplicitMappings = nullptr);

	/** Return true when exactly one same-name project UFont exists but is rejected as unusable. */
	static bool HasInvalidProjectFontAsset(const FString& FontFamily);

	/** Resolve a font declaration into the shared diagnostic state used by import reports. */
	static FFontResolutionDiagnostic ResolveFontDiagnostic(
		const FString& FontFamily,
		const FString& PostScriptName,
		const FString& Weight,
		const FString& Style,
		bool bUnsupportedInput,
		bool bFontUrlIgnored = false);

	/** Parse a weight string ("bold", "700", etc.) to numeric value (100-900). */
	static int32 ParseWeightValue(const FString& Weight);

	/** Map a parsed 100..900 weight value onto its UMG typeface name (unknown values map to Regular). */
	static FString TypefaceNameForWeightValue(int32 WeightValue);

	/** Clear the font asset path cache */
	static void ClearCache();

	/** Seed a known font-family to asset-path mapping for the current deterministic import. */
	static void RegisterFontAssetPath(const FString& FontFamily, const FString& ObjectPath);

	/** True when any icon family glyph table is currently registered. */
	static bool HasRegisteredIconGlyphs();

	/** Register glyph/name lookups for a known icon family. */
	static void RegisterKnownIconGlyphs(
		const FString& FontFamily,
		const TMap<FString, FString>& Glyphs,
		const TMap<FString, FString>& IconNames);
};


