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
	FPenDocument& Document,
	FOnComplete OnComplete)
{
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Document, ImageRefs);

	if (ImageRefs.Num() == 0)
	{
		UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: No image refs to resolve"));
		if (OnComplete) OnComplete(true);
		return;
	}

	UE_LOG(LogPenToUMG, Log, TEXT("FigmaImageExporter: Resolving %d image refs"), ImageRefs.Num());

	// Figma GET /v1/files/:key/images 返回 { "meta": { "images": { "imageRef": "url", ... } } }
	Client.GetFileImages(FileKey,
		[&Document, OnComplete](bool bSuccess, const FString& Body)
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
			FFigmaParser::ApplyImageUrls(Document, RefToUrl);

			if (OnComplete) OnComplete(true);
		});
}
