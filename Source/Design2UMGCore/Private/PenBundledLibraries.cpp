// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenBundledLibraries.h"

namespace PenBundledLibraries
{
	namespace
	{
		// Library name -> serialized "variables" JSON. Registration and lookup both run on
		// the game thread (import pipeline entry points), so no locking is needed.
		TMap<FString, FString>& GetRegistry()
		{
			static TMap<FString, FString> Registry;
			return Registry;
		}
	}

	void RegisterLibraryVariables(const FString& LibraryName, const FString& VariablesJson)
	{
		if (LibraryName.IsEmpty() || VariablesJson.IsEmpty())
		{
			return;
		}
		GetRegistry().Add(LibraryName, VariablesJson);
	}

	bool TryGetVariablesJson(const FString& LibraryName, FString& OutJson)
	{
		if (const FString* Json = GetRegistry().Find(LibraryName))
		{
			OutJson = *Json;
			return true;
		}
		return false;
	}

	int32 NumRegisteredLibraries()
	{
		return GetRegistry().Num();
	}
}
