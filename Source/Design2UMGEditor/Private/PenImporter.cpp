// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenImporter.h"
#include "Design2UMGLog.h"
#include "FontResolver.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "PenMCPBridge.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

bool FPenImporter::ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(2.0f, FText::FromString(TEXT("Importing .pen file...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design file...")));
	FPenDocument Document;
	if (!FPenParser::ParseFromFile(FilePath, Document))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to parse file: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	FFontResolver::FindMissingFonts(Document);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	OutData = FDesign2UMGConverter::Convert(Document);
	return true;
}

bool FPenImporter::ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(2.0f, FText::FromString(TEXT("Importing design...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design...")));
	FPenDocument Document;
	if (!FPenParser::ParseDocument(JsonString, Document))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to parse JSON"));
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	FFontResolver::FindMissingFonts(Document);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	OutData = FDesign2UMGConverter::Convert(Document);
	return true;
}

bool FPenImporter::ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(3.0f, FText::FromString(TEXT("Importing .pen frame...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Reading design file...")));
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to read file: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design file...")));
	TSharedPtr<FJsonObject> DocObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, DocObj) || !DocObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: JSON parse failed: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	if (!FPenMCPBridge::ConvertFromParsedDoc(DocObj, OutData, TargetNodeId))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: ConvertFromParsedDoc failed for node %s"), *TargetNodeId);
		return false;
	}

	return true;
}
