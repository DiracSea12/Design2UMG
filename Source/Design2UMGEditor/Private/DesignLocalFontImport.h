// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;

namespace DesignLocalFontImport
{
	struct FFace
	{
		FString Path;
		FString Family;
		FString PostScriptName;
		int32 Weight = 400;
		bool bItalic = false;
	};

	/** Reads internal identity, never the basename. Collections and variable faces are not imported. */
	bool ReadFace(const FString& Path, FFace& OutFace, FString& OutError);
	/** Creates/extends a runtime UFont using embedded data; returns save failures to the caller. */
	bool ImportFace(const FFace& Face, const FString& PackageName, FString& OutError, const FString& TypefaceName = FString());
	class FSourceIndex
	{
	public:
		/** Lazy index for one import; discard between imports so newly added fonts are seen. */
		const TArray<FFace>& GetFaces();
#if WITH_DEV_AUTOMATION_TESTS
		int32 GetMetadataReadCount() const { return MetadataReadCount; }
		int32 GetMetadataCacheHits() const { return MetadataCacheHits; }
#endif
	private:
		TArray<FFace> Faces;
		bool bLoaded = false;
#if WITH_DEV_AUTOMATION_TESTS
		int32 MetadataReadCount = 0;
		int32 MetadataCacheHits = 0;
#endif
	};
	/** Prepares only fonts used by this document. Explicit user mappings are never modified. */
	bool Prepare(const FDesignDocument& Document, TArray<FString>& OutWarnings, FString& OutError,
		FSourceIndex* SourceIndex = nullptr);
}
