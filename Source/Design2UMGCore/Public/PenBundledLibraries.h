// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Registry of Pencil design library variables ("shadcn", "lunaris", ...).
 *
 * Designs reference these libraries through their imports map (for example
 * {"Y": "pencil:shadcn.lib.pen"}); the library documents live inside the
 * Pencil VS Code extension (node_modules/@ha/pencil-editor/data/*.lib.pen)
 * and are registered here at import time by the editor-side resource loader
 * (PencilExtensionResources::EnsureBundledLibrariesRegistered). The registry
 * starts empty: without registration, imports resolve through the regular
 * unresolved-ref path.
 */
namespace PenBundledLibraries
{
	/** Registers (or replaces) a library's serialized "variables" JSON under its name (e.g. "shadcn"). */
	DESIGN2UMGCORE_API void RegisterLibraryVariables(const FString& LibraryName, const FString& VariablesJson);

	/** Returns the serialized "variables" JSON of a registered library (e.g. "shadcn"). */
	DESIGN2UMGCORE_API bool TryGetVariablesJson(const FString& LibraryName, FString& OutJson);

	/** Number of registered libraries. */
	DESIGN2UMGCORE_API int32 NumRegisteredLibraries();
}
