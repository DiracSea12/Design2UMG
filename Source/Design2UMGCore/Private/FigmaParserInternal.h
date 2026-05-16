// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParser.h"

namespace FigmaParserInternal
{
	bool ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument);
	bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey);
	void CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs);
	void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl);
}