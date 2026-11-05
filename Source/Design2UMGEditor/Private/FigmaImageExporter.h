// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
class FFigmaHttpClient;

/**
 * Figma image export helper.
 *
 * Collects imageRef values from an FDesignDocument, resolves them to concrete
 * URLs via the Figma images API, and writes the results back into the node tree.
 * The URLs can then be downloaded and imported by FDesignRemoteImageImporter.
 */
class DESIGN2UMGEDITOR_API FFigmaImageExporter
{
public:
	using FOnComplete = TFunction<void(bool bSuccess)>;

	/**
	 * Resolve all imageRef entries in the document to concrete URLs asynchronously.
	 * @param Client Preconfigured HTTP client with a valid token.
	 * @param FileKey Figma file key.
	 * @param Document Target document. Shared ownership keeps async usage safe.
	 * @param OnComplete Completion callback.
	 */
	static void ResolveImageRefs(
		FFigmaHttpClient& Client,
		const FString& FileKey,
		TSharedRef<FDesignDocument> Document,
		FOnComplete OnComplete);
};
