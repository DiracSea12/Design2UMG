// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;

/**
 * Runtime resources shipped inside the Pencil VS Code extension.
 *
 * The icon codepoint index (icon-index.json) maps icon names of every font family the
 * Pencil editor supports (lucide, Material Symbols variants, phosphor, feather, ...) onto
 * concrete font codepoints. Loading it once per process lets the converter render real
 * icon glyphs instead of ligature name text.
 */
namespace PencilExtensionResources
{
	/** Locate icon-index.json inside the installed extension, parse it, and register the per-family glyph tables into FFontResolver. Idempotent per process. */
	void EnsureIconGlyphsRegistered();

	/** True after a successful icon-index registration in this process. */
	bool IsIconIndexRegistered();

	/** Relative paths of the icon index inside an extension root, in priority order. */
	void GetIconIndexRelativeCandidates(TArray<FString>& OutRelativePaths);

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
	 * library variables and the extension resources are missing, annotate the document
	 * metadata, keep logging the gap for every import, and surface one editor notification
	 * per session (suppressible through the plugin settings).
	 */
	void ReportPenResourceGaps(FDesignDocument& Document);
}
