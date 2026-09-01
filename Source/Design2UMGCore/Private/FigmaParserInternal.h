// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParser.h"

#include "DesignJsonUtils.h"
#include "Containers/Map.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FigmaParserInternal
{
	namespace Private
	{
		void SetStringFieldIfPresent(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& OutValue);
	}

	/** Copies a JSON field into the metadata extension map when present. */
	inline void AddJsonExtensionIfPresent(
		const TSharedPtr<FJsonObject>& Obj,
		TMap<FString, FString>& Extensions,
		const FString& SourceKey,
		const FString& ExtKey)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(SourceKey);
		if (FieldValue.IsValid())
		{
			Extensions.Add(ExtKey, DesignJsonUtils::ToPrettyString(FieldValue));
		}
	}

	bool ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument);
	bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey);
	void CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs);
	void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl);
}
