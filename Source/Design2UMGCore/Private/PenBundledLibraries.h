// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Variables tables of the Pencil built-in design libraries.
 *
 * Designs reference these libraries through their imports map (for example
 * {"Y": "pencil:shadcn.lib.pen"}); the library documents themselves are not
 * embedded in the referencing design, so the converter ships each library's
 * "variables" block as a JSON literal.
 */
namespace PenBundledLibraries
{
	/** Returns the serialized "variables" JSON of a bundled library (e.g. "shadcn"). */
	DESIGN2UMGCORE_API bool TryGetVariablesJson(const FString& LibraryName, FString& OutJson);
}
