// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;

/**
 * Load installed font descriptions, icon codepoints and bundled design-library components and variables from the Pencil extension.
 * Register these resources before converting icons or resolving library imports.
 */
namespace PencilExtensionResources
{
	struct FFontStyle
	{
		FString Family;
		FString Url;
		int32 Weight = 400;
		bool bItalic = false;
		bool bVariable = false;
		TOptional<FVector2D> WeightRange;

		int32 ResolveWeight(int32 RequestedWeight) const;
	};

	/** Read literal font descriptions for the requested families (empty selects all) without evaluating JavaScript. */
	bool ExtractFontStyles(const FString& Bundle, const TSet<FString>& Families,
		TArray<FFontStyle>& OutStyles, FString& OutError);
	/** Select a current editor font description by real family, style and effective weight. */
	bool FindFontStyle(const FString& Family, int32 Weight, bool bItalic, FFontStyle& OutStyle);

	/** Read the installed editor bundle's icon registry and register its family glyphs. Idempotent per process. */
	void EnsureIconGlyphsRegistered();

	/** Current editor entry bundle containing the icon registry, relative to the extension root. */
	void GetIconIndexRelativeCandidates(TArray<FString>& OutRelativePaths);

	/** Extract the current unescaped JSON.parse('...') registry as data; never evaluate bundle code. */
	bool ExtractIconIndexJson(const FString& Bundle, FString& OutJson, FString& OutError);

	/** Locate the extension's bundled library documents (*.lib.pen), register their component definitions and variables into PenBundledLibraries. Idempotent per process. */
	void EnsureBundledLibrariesRegistered();

	/** Relative directories of the bundled library documents inside an extension root, in priority order. */
	void GetLibraryPenRelativeCandidates(TArray<FString>& OutRelativeDirs);

	/**
	 * Derive the registry library name from a bundled document file name ("shadcn.lib.pen" ->
	 * "shadcn"), matching the key PenParser::ParsePencilLibraryId queries for "pencil:<name>.lib.pen"
	 * imports. Returns false when the name is not a *.lib.pen document.
	 */
	bool TryDeriveLibraryNameFromFileName(const FString& FileName, FString& OutLibraryName);

	/** Register every *.lib.pen document in a directory into PenBundledLibraries; returns the number of registered documents. */
	int32 RegisterLibrariesFromDirectory(const FString& DataDir);

	/**
	 * Parse a normalized {sets:[...]} icon registry into per-family lookup tables.
	 * Icon keys are normalized with FFontResolver::NormalizeIconNameKey and family keys are the
	 * raw family names from the file ("lucide", "Material Symbols Rounded", ...), so registering
	 * the result through FFontResolver::RegisterKnownIconGlyphs makes queries hit directly.
	 */
	bool ParseIconIndexJson(
		const FString& Json,
		TMap<FString, TMap<FString, FString>>& OutGlyphsByFamily,
		TMap<FString, TMap<FString, FString>>& OutNamesByFamily,
		FString& OutError);

	/**
	 * After a pen document parses: when the document actually uses icon nodes or imported
	 * library variables and the matching extension resources are missing, annotate the
	 * document metadata, keep logging the gap for every import, and surface one editor
	 * notification per session (suppressible through the plugin settings).
	 */
	void ReportPenResourceGaps(FDesignDocument& Document);
}
