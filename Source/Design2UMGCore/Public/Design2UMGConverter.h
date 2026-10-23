// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "UMGSchema.h"
#include "FontResolver.h"

struct FDesignDocument;

struct DESIGN2UMGCORE_API FDesign2UMGConversionSettings
{
	UDesign2UMGSettings::FResolvedPlaceholderHatchStyle PlaceholderHatchStyle;
	float LuminosityFitOpacityCap = UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
	FDesign2UMGFontMappings ExplicitFontMappings;
	FString DefaultFontAssetPath;
	TMap<FString, FString> IconFontAssetPaths;
	FString DefaultIconFontAssetPath;
	FString MissingIconFontPlaceholder;

	static FDesign2UMGConversionSettings Defaults();
	static FDesign2UMGConversionSettings FromSettings(const UDesign2UMGSettings* Settings);
};

/**
 * Converts shared design IR into normalized UMG generation IR.
 * Font mapping policy is captured by Settings and passed through measurement and rich-text generation.
 * Conversion runs on the game thread and still requires the current project font assets and Slate renderer;
 * it is not a renderer-independent pure function.
 */
class DESIGN2UMGCORE_API FDesign2UMGConverter
{
public:
	/** Prepares fonts after reference/script expansion and before measurement; optional in Core-only callers.
	 * GlyphCoverage limits source preparation to styles declared by the current document. */
	enum class EFontPreparation : uint8 { ImportSources, GlyphCoverage };
	using FPrepareFonts = TFunction<bool(const FDesignDocument&, TArray<FString>&, FString&, EFontPreparation)>;

	/**
	 * Convert a parsed design document into typed widget data ready for Blueprint generation.
	 * @param Document Parsed design-side IR.
	 * @return Generated UMG widget tree and associated metadata.
	 */
	static FUMGWidgetData Convert(const FDesignDocument& Document, const FDesign2UMGConversionSettings& Settings,
		const FPrepareFonts& PrepareFonts = FPrepareFonts());
};
