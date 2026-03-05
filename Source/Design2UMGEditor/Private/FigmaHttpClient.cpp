// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

static const FString FigmaApiBase = TEXT("https://api.figma.com");

void FFigmaHttpClient::GetFile(const FString& FileKey, FOnComplete OnComplete)
{
	SendGet(FString::Printf(TEXT("%s/v1/files/%s?geometry=paths"), *FigmaApiBase, *FileKey), MoveTemp(OnComplete));
}

void FFigmaHttpClient::GetFileImages(const FString& FileKey, FOnComplete OnComplete)
{
	SendGet(FString::Printf(TEXT("%s/v1/files/%s/images"), *FigmaApiBase, *FileKey), MoveTemp(OnComplete));
}

void FFigmaHttpClient::ExportImages(const FString& FileKey, const TArray<FString>& NodeIds, FOnComplete OnComplete)
{
	FString Ids = FString::Join(NodeIds, TEXT(","));
	SendGet(FString::Printf(TEXT("%s/v1/images/%s?ids=%s&format=png&scale=2"), *FigmaApiBase, *FileKey, *Ids), MoveTemp(OnComplete));
}

void FFigmaHttpClient::SendGet(const FString& Url, FOnComplete OnComplete)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("X-Figma-Token"), Token);

	Request->OnProcessRequestComplete().BindLambda(
		[OnComplete = MoveTemp(OnComplete)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!bConnected || !Response.IsValid())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: Connection failed"));
				if (OnComplete) OnComplete(false, TEXT(""));
				return;
			}

			const int32 Code = Response->GetResponseCode();
			if (Code < 200 || Code >= 300)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: HTTP %d"), Code);
				if (OnComplete) OnComplete(false, Response->GetContentAsString());
				return;
			}

			if (OnComplete) OnComplete(true, Response->GetContentAsString());
		});

	Request->ProcessRequest();
}

