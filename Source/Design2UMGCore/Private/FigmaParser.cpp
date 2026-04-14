// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParser.h"
#include "FigmaParserInternal.h"

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	return FigmaParserInternal::ParseDocument(FigmaJson, OutDocument);
}

bool FFigmaParser::ExtractFileKey(const FString& UrlOrKey, FString& OutKey)
{
	return FigmaParserInternal::ExtractFileKey(UrlOrKey, OutKey);
}

void FFigmaParser::CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs)
{
	FigmaParserInternal::CollectImageRefs(Document, OutImageRefs);
}

void FFigmaParser::ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	FigmaParserInternal::ApplyImageUrls(Document, ImageRefToUrl);
}