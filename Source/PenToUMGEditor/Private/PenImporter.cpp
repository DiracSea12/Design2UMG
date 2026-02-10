// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenImporter.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenToUMGConverter.h"
#include "UMGSchema.h"

bool FPenImporter::ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData)
{
	FPenDocument Document;
	if (!FPenParser::ParseFromFile(FilePath, Document))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: Failed to parse file: %s"), *FilePath);
		return false;
	}

	OutData = FPenToUMGConverter::Convert(Document);
	return true;
}

bool FPenImporter::ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData)
{
	FPenDocument Document;
	if (!FPenParser::ParseDocument(JsonString, Document))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: Failed to parse JSON"));
		return false;
	}

	OutData = FPenToUMGConverter::Convert(Document);
	return true;
}
