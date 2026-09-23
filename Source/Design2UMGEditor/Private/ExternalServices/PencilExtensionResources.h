// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;

/**
 * Load icon codepoints and bundled design-library variables from the Pencil extension.
 * Register these resources before converting icons or resolving library imports.
 */
namespace PencilExtensionResources
{
	/** Locate icon-index.json inside the installed extension, parse it, and register the per-family glyph tables into FFontResolver. Idempotent per process. */
	void EnsureIconGlyphsRegistered();

	/** Relative paths of the icon index inside an extension root, in priority order. */
	void GetIconIndexRelativeCandidates(TArray<FString>& OutRelativePaths);

	/** Locate the extension's bundled library documents (*.lib.pen), extract their variables, and register them into PenBundledLibraries. Idempotent per process. */
	void EnsureBundledLibrariesRegistered();

	/** Relative directories of the bundled library documents inside an extension root, in priority order. */
	void GetLibraryPenRelativeCandidates(TArray<FString>& OutRelativeDirs);

	/**
	 * Derive the registry library name from a bundled document file name ("shadcn.lib.pen" ->
	 * "shadcn"), matching the key PenParser::ParsePencilLibraryId queries for "pencil:<name>.lib.pen"
	 * imports. Returns false when the name is not a *.lib.pen document.
	 */
	bool TryDeriveLibraryNameFromFileName(const FString& FileName, FString& OutLibraryName);

	/** Register every *.lib.pen document in a directory into PenBundledLibraries; returns the number of registered tables. */
	int32 RegisterLibrariesFromDirectory(const FString& DataDir);

	/**
	 * Extract a .lib.pen payload's top-level "variables" object and serialize it for
	 * PenBundledLibraries::RegisterLibraryVariables. Returns false with OutError when the
	 * payload is not valid JSON or has no "variables" object.
	 */
	bool ParseLibPenVariables(const FString& LibPenJson, FString& OutVariablesJson, FString& OutError);

	/**
	 * Parse an icon-index.json payload into per-family lookup tables.
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
