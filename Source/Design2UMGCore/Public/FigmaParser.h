#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

/**
 * Parses Figma export JSON into the shared design IR and related asset metadata.
 */
class DESIGN2UMGCORE_API FFigmaParser
{
public:
	/**
	 * Parse a Figma export JSON string into a design document.
	 * @param FigmaJson Source Figma export JSON text.
	 * @param OutDocument Parsed document on success.
	 * @return `true` when parsing succeeds.
	 */
	static bool ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument);

	/**
	 * Extract a Figma file key from a URL or return the key unchanged.
	 * @param UrlOrKey A full Figma URL or raw file key.
	 * @param OutKey Extracted file key on success.
	 * @return `true` when a key can be resolved.
	 */
	static bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey);

	/**
	 * Collect image reference ids used by the parsed document.
	 * @param Document Parsed design document.
	 * @param OutImageRefs Output set of image reference ids.
	 */
	static void CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs);

	/**
	 * Apply resolved image URLs to matching image reference ids in the document.
	 * @param Document Parsed design document to mutate.
	 * @param ImageRefToUrl Mapping from image ref id to resolved URL.
	 */
	static void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl);
};

