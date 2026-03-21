#pragma once

#include "CoreMinimal.h"

#include "Dom/JsonObject.h"

struct FDesignDocument;

class DESIGN2UMGCORE_API FPenParser
{
public:
	static bool ParseDocument(const FString& JsonString, FDesignDocument& OutDocument);
	static bool ParseDocumentObject(const TSharedPtr<FJsonObject>& JsonObject, FDesignDocument& OutDocument);
	static bool ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument);
};

