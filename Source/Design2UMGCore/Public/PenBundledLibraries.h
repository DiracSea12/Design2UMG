// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Variables from Pencil bundled design libraries, registered by the editor resource loader.
 * Imports use the registry; unregistered libraries follow unresolved-reference handling.
 */
namespace PenBundledLibraries
{
	/** Registers (or replaces) a library's serialized "variables" JSON under its name (e.g. "shadcn"). */
	DESIGN2UMGCORE_API void RegisterLibraryVariables(const FString& LibraryName, const FString& VariablesJson);

	/** Returns the serialized "variables" JSON of a registered library (e.g. "shadcn"). */
	DESIGN2UMGCORE_API bool TryGetVariablesJson(const FString& LibraryName, FString& OutJson);
}
