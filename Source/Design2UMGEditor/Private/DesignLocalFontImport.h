// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "FontResolver.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"

struct FDesignDocument;
struct FDesign2UMGConversionSettings;

namespace DesignLocalFontImport
{
	struct FFace
	{
		FString Path;
		FString Family;
		FString PostScriptName;
		int32 Weight = 400;
		bool bItalic = false;
		bool bVariable = false;
		TOptional<FVector2D> WeightRange;
	};

	/** Reads internal identity, never the basename. Collections and variable faces require static conversion before importing. */
	bool ReadFace(const FString& Path, FFace& OutFace, FString& OutError);
	/** Creates/extends a runtime UFont using embedded data; returns save failures to the caller. */
	bool ImportFace(const FFace& Face, const FString& PackageName, FString& OutError, const FString& TypefaceName = FString(),
		const FString& RegisteredFamily = FString());
	class FSourceIndex
	{
	public:
		/** Source metadata only; matching variable/compressed faces are converted on demand.
		 * Lazy for one import; discard between imports so newly added fonts are seen. */
		const TArray<FFace>& GetFaces();
		const TArray<FString>& GetUninspectedCompressedSources() const { return UninspectedCompressedSources; }
		const FString& GetError() const { return DiscoveryError; }
	private:
		TArray<FFace> Faces;
		TArray<FString> UninspectedCompressedSources;
		FString DiscoveryError;
		bool bLoaded = false;
	};
	/** Extends this import's resolved font array and reuses its face digests, then prepares only Targets. No source imports or downloads. */
	bool PrepareGlyphFallbacks(const FDesignDocument& Targets, const FDesign2UMGFontMappings& FontMappings,
		TArray<FSlateFontInfo>& DocumentFonts, TMap<FFontData, FString>& FontDataHashes, TArray<FString>& OutWarnings, FString& OutError);
	/** Prepares used fonts and records their real declared faces. Declared-only preparation never searches other sources. */
	bool Prepare(const FDesignDocument& Document, const FDesign2UMGConversionSettings& Settings,
		TArray<FString>& OutWarnings, FString& OutError, FSourceIndex* SourceIndex = nullptr, bool bDeclaredOnly = false);
}
