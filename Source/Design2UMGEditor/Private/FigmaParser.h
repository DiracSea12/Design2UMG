#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

class FFigmaParser
{
public:
	static bool ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument);
	static bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey);
	static void CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs);
	static void ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl);
};

