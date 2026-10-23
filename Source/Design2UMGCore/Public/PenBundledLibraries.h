// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Components and variables from Pencil bundled design libraries, registered by the editor resource loader.
 * Imports use the registry; unregistered libraries follow unresolved-reference handling.
 */
namespace PenBundledLibraries
{
	/** Complete library document owning component definitions, variables and their source path. */
	DESIGN2UMGCORE_API void RegisterLibraryDocument(const FString& LibraryName, const FString& DocumentJson, const FString& SourceFilePath = FString());
	DESIGN2UMGCORE_API bool TryGetDocumentJson(const FString& LibraryName, FString& OutJson, FString* OutSourceFilePath = nullptr);
}
