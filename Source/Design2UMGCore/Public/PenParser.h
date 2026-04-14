#pragma once

#include "CoreMinimal.h"

#include "Dom/JsonObject.h"

struct FDesignDocument;

/**
 * Parses Pencil `.pen` payloads into the shared design IR.
 */
class DESIGN2UMGCORE_API FPenParser
{
public:
	/**
	 * Parse a raw Pencil JSON string into a design document.
	 * @param JsonString Source Pencil JSON text.
	 * @param OutDocument Parsed document on success.
	 * @return `true` when parsing succeeds.
	 */
	static bool ParseDocument(const FString& JsonString, FDesignDocument& OutDocument);

	/**
	 * Parse an already-loaded Pencil JSON object into a design document.
	 * @param JsonObject Source Pencil document object.
	 * @param OutDocument Parsed document on success.
	 * @return `true` when parsing succeeds.
	 */
	static bool ParseDocumentObject(const TSharedPtr<FJsonObject>& JsonObject, FDesignDocument& OutDocument);

	/**
	 * Load and parse a Pencil `.pen` file from disk.
	 * @param FilePath Absolute or project-relative file path.
	 * @param OutDocument Parsed document on success.
	 * @return `true` when the file loads and parses successfully.
	 */
	static bool ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument);
};

