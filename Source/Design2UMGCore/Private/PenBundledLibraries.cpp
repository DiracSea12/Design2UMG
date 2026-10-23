// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenBundledLibraries.h"

namespace PenBundledLibraries
{
	namespace
	{
		// Registration and lookup run on the game thread through the import pipeline.
		struct FLibraryDocument
		{
			FString Json;
			FString SourceFilePath;
		};

		TMap<FString, FLibraryDocument>& GetDocuments()
		{
			static TMap<FString, FLibraryDocument> Documents;
			return Documents;
		}
	}

	void RegisterLibraryDocument(const FString& LibraryName, const FString& DocumentJson, const FString& SourceFilePath)
	{
		GetDocuments().Add(LibraryName, {DocumentJson, SourceFilePath});
	}

	bool TryGetDocumentJson(const FString& LibraryName, FString& OutJson, FString* OutSourceFilePath)
	{
		if (const FLibraryDocument* Document = GetDocuments().Find(LibraryName))
		{
			OutJson = Document->Json;
			if (OutSourceFilePath) *OutSourceFilePath = Document->SourceFilePath;
			return true;
		}
		return false;
	}
}
