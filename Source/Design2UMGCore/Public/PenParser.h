// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Dom/JsonObject.h"

/**
 * Parses Pencil `.pen` payloads into the shared design IR.
 */
class DESIGN2UMGCORE_API FPenParser
{
public:
	/** Parse Pencil source JSON with line/block comments; all other JSON syntax stays strict. */
	static bool ParseSourceObject(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject);

	/** Load a .pen document and resolve its imports relative to this file. */
	static bool ParseDocumentFile(const FString& FilePath, FDesignDocument& OutDocument);

	/**
	 * Parse a raw Pencil JSON string into a design document.
	 * Replaces OutDocument on success and leaves it empty on failure.
	 * @param JsonString Source Pencil JSON text.
	 * @param OutDocument Parsed document on success.
	 * @param SourceFilePath Native source filename, used to resolve relative imports and assets.
	 * @return `true` when parsing succeeds.
	 */
	static bool ParseDocument(const FString& JsonString, FDesignDocument& OutDocument, const FString& SourceFilePath = FString());

	/**
	 * Parse an already-loaded Pencil JSON object into a design document.
	 * Replaces OutDocument on success and leaves it empty on failure.
	 * @param JsonObject Source Pencil document object.
	 * @param OutDocument Parsed document on success.
	 * @param SourceFilePath Native source filename, used to resolve relative imports and assets.
	 * @return `true` when parsing succeeds.
	 */
	static bool ParseDocumentObject(const TSharedPtr<FJsonObject>& JsonObject, FDesignDocument& OutDocument, const FString& SourceFilePath = FString());
};

