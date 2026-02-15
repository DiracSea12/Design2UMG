// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenImporter.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenToUMGConverter.h"
#include "PenMCPBridge.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

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

bool FPenImporter::ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: Failed to read file: %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> DocObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, DocObj) || !DocObj.IsValid())
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: JSON parse failed: %s"), *FilePath);
		return false;
	}

	if (!FPenMCPBridge::ConvertFromParsedDoc(DocObj, OutData, TargetNodeId))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: ConvertFromParsedDoc failed for node %s"), *TargetNodeId);
		return false;
	}

	return true;
}
