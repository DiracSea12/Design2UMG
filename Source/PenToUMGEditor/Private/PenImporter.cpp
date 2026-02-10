// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenImporter.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenToUMGConverter.h"
#include "PenMCPBridge.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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

	// 提取 children 数组
	FString NodesJson;
	if (DocObj->HasField(TEXT("children")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = DocObj->GetArrayField(TEXT("children"));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&NodesJson);
		FJsonSerializer::Serialize(Arr, W);
	}

	// 提取 variables/themes/fonts
	TSharedPtr<FJsonObject> VarsObj = MakeShared<FJsonObject>();
	if (DocObj->HasField(TEXT("variables")))
		VarsObj->SetField(TEXT("variables"), DocObj->TryGetField(TEXT("variables")));
	if (DocObj->HasField(TEXT("themes")))
		VarsObj->SetField(TEXT("themes"), DocObj->TryGetField(TEXT("themes")));
	if (DocObj->HasField(TEXT("fonts")))
		VarsObj->SetField(TEXT("fonts"), DocObj->TryGetField(TEXT("fonts")));

	FString VarsJson;
	TSharedRef<TJsonWriter<>> VW = TJsonWriterFactory<>::Create(&VarsJson);
	FJsonSerializer::Serialize(VarsObj.ToSharedRef(), VW);

	if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, OutData, TargetNodeId))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImporter: ConvertFromMCP failed for node %s"), *TargetNodeId);
		return false;
	}

	return true;
}
