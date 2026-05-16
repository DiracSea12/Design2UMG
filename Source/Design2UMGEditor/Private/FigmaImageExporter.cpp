// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"
#include "DesignSchema.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FFigmaImageExporter::ResolveImageRefs(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FDesignDocument> Document,
	FOnComplete OnComplete)
{
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(*Document, ImageRefs);

	if (ImageRefs.Num() == 0)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: No image refs to resolve"));
		if (OnComplete) OnComplete(true);
		return;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Resolving %d image refs"), ImageRefs.Num());

	// Figma GET /v1/files/:key/images returns { "meta": { "images": { "imageRef": "url", ... } } }.
	Client.GetFileImages(FileKey,
		[Document, OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: GetFileImages failed"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: Failed to parse images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* MetaObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("meta"), MetaObj) || !MetaObj || !(*MetaObj).IsValid())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: No 'meta' in images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!(*MetaObj)->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: No 'images' in meta"));
				if (OnComplete) OnComplete(true);
				return;
			}

			TMap<FString, FString> RefToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
				{
					RefToUrl.Add(Pair.Key, Url);
				}
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Resolved %d image URLs"), RefToUrl.Num());
			FFigmaParser::ApplyImageUrls(*Document, RefToUrl);

			if (OnComplete) OnComplete(true);
		});
}

// ---------------------------------------------------------------------------
// ExportVectorNodes
// ---------------------------------------------------------------------------

static void CollectVectorNodeIds(const TArray<TSharedPtr<FDesignNode>>& Nodes, TArray<FString>& OutIds)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EDesignNodeType::Path && !Node->Id.IsEmpty())
			OutIds.Add(Node->Id);
		CollectVectorNodeIds(Node->Children, OutIds);
	}
}

void FFigmaImageExporter::ApplyExportedImages(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& IdToUrl)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EDesignNodeType::Path)
		{
			if (const FString* Url = IdToUrl.Find(Node->Id))
			{
				Node->Type = EDesignNodeType::Rectangle;
				Node->Visual.Fill.Type = EDesignFillType::Image;
				Node->Visual.Fill.Image.Url = *Url;
				Node->Visual.Fills.Empty();
				Node->Visual.Fills.Add(Node->Visual.Fill);
			}
		}
		ApplyExportedImages(Node->Children, IdToUrl);
	}
}

void FFigmaImageExporter::ExportVectorNodes(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FDesignDocument> Document,
	FOnVectorExportComplete OnComplete)
{
	TArray<FString> VectorIds;
	CollectVectorNodeIds(Document->Children, VectorIds);

	if (VectorIds.Num() == 0)
	{
		if (OnComplete) OnComplete(true, {});
		return;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Exporting %d vector nodes as PNG"), VectorIds.Num());

	Client.ExportImages(FileKey, VectorIds,
		[OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: ExportImages failed"));
				if (OnComplete) OnComplete(false, {});
				return;
			}

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: Failed to parse export response"));
				if (OnComplete) OnComplete(false, {});
				return;
			}

			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: No 'images' in export response"));
				if (OnComplete) OnComplete(true, {});
				return;
			}

			TMap<FString, FString> IdToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
					IdToUrl.Add(Pair.Key, Url);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Exported %d vector nodes"), IdToUrl.Num());
			if (OnComplete) OnComplete(true, IdToUrl);
		});
}


