// Copyright Epic Games, Inc. All Rights Reserved.

#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FigmaHttpClient.h"
#include "PenToUMGLog.h"
#include "PenSchema.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FFigmaImageExporter::ResolveImageRefs(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FPenDocument> Document,
	FOnComplete OnComplete)
{
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(*Document, ImageRefs);

	if (ImageRefs.Num() == 0)
	{
		UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: No image refs to resolve"));
		if (OnComplete) OnComplete(true);
		return;
	}

	UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: Resolving %d image refs"), ImageRefs.Num());

	// Figma GET /v1/files/:key/images 返回 { "meta": { "images": { "imageRef": "url", ... } } }
	Client.GetFileImages(FileKey,
		[Document, OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogPenToUMG, Error, TEXT("FigmaImageExporter: GetFileImages failed"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				UE_LOG(LogPenToUMG, Error, TEXT("FigmaImageExporter: Failed to parse images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* MetaObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("meta"), MetaObj) || !MetaObj || !(*MetaObj).IsValid())
			{
				UE_LOG(LogPenToUMG, Error, TEXT("FigmaImageExporter: No 'meta' in images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!(*MetaObj)->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("FigmaImageExporter: No 'images' in meta"));
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

			UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: Resolved %d image URLs"), RefToUrl.Num());
			FFigmaParser::ApplyImageUrls(*Document, RefToUrl);

			if (OnComplete) OnComplete(true);
		});
}

// ---------------------------------------------------------------------------
// ExportVectorNodes
// ---------------------------------------------------------------------------

static void CollectVectorNodeIds(const TArray<TSharedPtr<FPenNode>>& Nodes, TArray<FString>& OutIds)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EPenNodeType::Path && !Node->Id.IsEmpty())
			OutIds.Add(Node->Id);
		CollectVectorNodeIds(Node->Children, OutIds);
	}
}

static void ApplyExportedImages(TArray<TSharedPtr<FPenNode>>& Nodes, const TMap<FString, FString>& IdToUrl)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EPenNodeType::Path)
		{
			if (const FString* Url = IdToUrl.Find(Node->Id))
			{
				Node->Type = EPenNodeType::Rectangle;
				Node->Fill.Type = EPenFillType::Image;
				Node->Fill.Image.Url = *Url;
				// 同步 Fills 数组
				Node->Fills.Empty();
				Node->Fills.Add(Node->Fill);
			}
		}
		ApplyExportedImages(Node->Children, IdToUrl);
	}
}

void FFigmaImageExporter::ExportVectorNodes(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FPenDocument> Document,
	FOnComplete OnComplete)
{
	TArray<FString> VectorIds;
	CollectVectorNodeIds(Document->Children, VectorIds);

	if (VectorIds.Num() == 0)
	{
		if (OnComplete) OnComplete(true);
		return;
	}

	UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: Exporting %d vector nodes as PNG"), VectorIds.Num());

	Client.ExportImages(FileKey, VectorIds,
		[Document, OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("FigmaImageExporter: ExportImages failed"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("FigmaImageExporter: Failed to parse export response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			// Figma GET /v1/images/:key 返回 { "images": { "nodeId": "url", ... } }
			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				UE_LOG(LogPenToUMG, Warning, TEXT("FigmaImageExporter: No 'images' in export response"));
				if (OnComplete) OnComplete(true);
				return;
			}

			TMap<FString, FString> IdToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
					IdToUrl.Add(Pair.Key, Url);
			}

			UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: Exported %d vector nodes"), IdToUrl.Num());
			ApplyExportedImages(Document->Children, IdToUrl);

			if (OnComplete) OnComplete(true);
		});
}
