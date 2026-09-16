// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"
#include "DesignHttpClientSlot.h"

#include "HttpModule.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

static const FString FigmaApiBase = TEXT("https://api.figma.com");

// Figma GetFile returns the full document JSON, which can be large; give the
// request more headroom than the MCP transport's 30s timeout.
constexpr float FigmaHttpRequestTimeoutSeconds = 60.0f;

FFigmaHttpClient::FFigmaHttpClient()
	: State(MakeShared<FDesignHttpClientSlot, ESPMode::ThreadSafe>())
{
}

FFigmaHttpClient::~FFigmaHttpClient()
{
	CancelActiveRequest();
}

void FFigmaHttpClient::GetFile(const FString& FileKey, FOnComplete OnComplete)
{
	SendGet(FString::Printf(TEXT("%s/v1/files/%s?geometry=paths"), *FigmaApiBase, *FileKey), MoveTemp(OnComplete));
}

void FFigmaHttpClient::GetFileImages(const FString& FileKey, FOnComplete OnComplete)
{
	SendGet(FString::Printf(TEXT("%s/v1/files/%s/images"), *FigmaApiBase, *FileKey), MoveTemp(OnComplete));
}

void FFigmaHttpClient::GetComponent(const FString& ComponentKey, FOnComplete OnComplete)
{
	SendGet(FString::Printf(TEXT("%s/v1/components/%s"), *FigmaApiBase, *FGenericPlatformHttp::UrlEncode(ComponentKey)), MoveTemp(OnComplete));
}

void FFigmaHttpClient::ExportImages(const FString& FileKey, const TArray<FString>& NodeIds, FOnComplete OnComplete)
{
	// Figma node ids can contain characters that are not URL-safe (e.g. ':');
	// encode each id but keep the ',' separator literal.
	TArray<FString> EncodedIds;
	EncodedIds.Reserve(NodeIds.Num());
	for (const FString& NodeId : NodeIds)
	{
		EncodedIds.Add(FPlatformHttp::UrlEncode(NodeId));
	}
	const FString Ids = FString::Join(EncodedIds, TEXT(","));
	SendGet(FString::Printf(TEXT("%s/v1/images/%s?ids=%s&format=png&scale=2"), *FigmaApiBase, *FileKey, *Ids), MoveTemp(OnComplete));
}

void FFigmaHttpClient::CancelActiveRequest()
{
	State->CancelActiveRequest();
}

void FFigmaHttpClient::SendGet(const FString& Url, FOnComplete OnComplete)
{
	CancelActiveRequest();

	const int32 RequestId = State->BeginRequest();
	TSharedRef<FOnComplete> SharedOnComplete = MakeShared<FOnComplete>(MoveTemp(OnComplete));

#if WITH_DEV_AUTOMATION_TESTS
	if (DesignHttpClientSlot::HasAutomationTransport())
	{
		const bool bStarted = DesignHttpClientSlot::StartAutomationTransport(
			RequestId,
			[State = State, RequestId, SharedOnComplete](
				bool bConnected,
				const FString& ResponseBody,
				int32 ResponseCode,
				const FString&)
			{
				if (!State->ConsumeCompletion(RequestId))
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("FigmaHttpClient: Request cancelled"));
					return;
				}

				if (!bConnected)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: Connection failed"));
					if (*SharedOnComplete)
					{
						(*SharedOnComplete)(false, TEXT(""));
					}
					return;
				}

				if (ResponseCode < 200 || ResponseCode >= 300)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: HTTP %d"), ResponseCode);
					if (*SharedOnComplete)
					{
						(*SharedOnComplete)(false, ResponseBody);
					}
					return;
				}

				if (*SharedOnComplete)
				{
					(*SharedOnComplete)(true, ResponseBody);
				}
			});

		if (!bStarted)
		{
			State->ClearActiveRequest(RequestId);
			if (*SharedOnComplete)
			{
				(*SharedOnComplete)(false, TEXT(""));
			}
		}
		return;
	}
#endif

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	State->BindActiveRequest(Request);
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("X-Figma-Token"), Token);
	Request->SetTimeout(FigmaHttpRequestTimeoutSeconds);

	Request->OnProcessRequestComplete().BindLambda(
		[State = State, RequestId, SharedOnComplete](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bConnected)
		{
			if (!State->ConsumeCompletion(RequestId))
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("FigmaHttpClient: Request cancelled"));
				return;
			}

			if (!bConnected || !Response.IsValid())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: Connection failed"));
				if (*SharedOnComplete) (*SharedOnComplete)(false, TEXT(""));
				return;
			}

			const int32 Code = Response->GetResponseCode();
			if (Code < 200 || Code >= 300)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: HTTP %d"), Code);
				if (*SharedOnComplete) (*SharedOnComplete)(false, Response->GetContentAsString());
				return;
			}

			if (*SharedOnComplete) (*SharedOnComplete)(true, Response->GetContentAsString());
		});

	if (!Request->ProcessRequest())
	{
		State->ClearActiveRequest(RequestId);
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: ProcessRequest failed"));
		if (*SharedOnComplete)
		{
			(*SharedOnComplete)(false, TEXT(""));
		}
	}
}
