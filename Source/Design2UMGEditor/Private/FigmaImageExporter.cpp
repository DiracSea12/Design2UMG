// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"

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
		[Document, ImageRefs, OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: GetFileImages failed"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			if (!DesignJsonUtils::TryParseObject(Body, Root))
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
				// Requested references require an images object in the export response.
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: No 'images' in meta; required image refs could not be resolved"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TMap<FString, FString> RefToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
				{
					RefToUrl.Add(FString(*Pair.Key), Url);
				}
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Resolved %d image URLs"), RefToUrl.Num());
			FFigmaParser::ApplyImageUrls(*Document, RefToUrl);

			TArray<FString> RequiredMissingRefs;
			for (const FString& Ref : ImageRefs)
			{
				if (RefToUrl.Contains(Ref))
				{
					continue;
				}
				RequiredMissingRefs.Add(Ref);
			}
			if (RequiredMissingRefs.Num() > 0)
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("FigmaImageExporter: %d required image ref(s) not resolved: %s"),
					RequiredMissingRefs.Num(), *FString::Join(RequiredMissingRefs, TEXT(", ")));
				if (OnComplete) OnComplete(false);
				return;
			}

			if (OnComplete) OnComplete(true);
		});
}
