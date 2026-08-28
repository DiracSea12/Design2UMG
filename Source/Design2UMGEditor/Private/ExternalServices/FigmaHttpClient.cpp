// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"
#include "Tests/HttpClientAutomationHooks.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

static const FString FigmaApiBase = TEXT("https://api.figma.com");

// Figma GetFile returns the full document JSON, which can be large; give the
// request more headroom than the MCP transport's 30s timeout.
constexpr float FigmaHttpRequestTimeoutSeconds = 60.0f;

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	TSharedPtr<FFigmaHttpClientAutomationHooks> GFigmaHttpClientAutomationHooks;
}
#endif

struct FFigmaHttpClientStateImpl
{
	FHttpRequestPtr ActiveRequest;
	int32 ActiveRequestId = 0;
	int32 NextRequestId = 0;
	TSet<int32> CancelledRequestIds;
};

FFigmaHttpClient::FFigmaHttpClient()
	: State(MakeShared<FFigmaHttpClientStateImpl, ESPMode::ThreadSafe>())
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

void FFigmaHttpClient::ExportImages(const FString& FileKey, const TArray<FString>& NodeIds, FOnComplete OnComplete)
{
	FString Ids = FString::Join(NodeIds, TEXT(","));
	SendGet(FString::Printf(TEXT("%s/v1/images/%s?ids=%s&format=png&scale=2"), *FigmaApiBase, *FileKey, *Ids), MoveTemp(OnComplete));
}

void FFigmaHttpClient::CancelActiveRequest()
{
	if (State->ActiveRequest.IsValid())
	{
		State->CancelledRequestIds.Add(State->ActiveRequestId);
		State->ActiveRequest->CancelRequest();
		State->ActiveRequest.Reset();
		State->ActiveRequestId = 0;
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (GFigmaHttpClientAutomationHooks.IsValid() && State->ActiveRequestId != 0)
	{
		State->CancelledRequestIds.Add(State->ActiveRequestId);
		if (GFigmaHttpClientAutomationHooks->CancelRequest)
		{
			GFigmaHttpClientAutomationHooks->CancelRequest(State->ActiveRequestId);
		}
		State->ActiveRequestId = 0;
	}
#endif
}

void FFigmaHttpClient::SendGet(const FString& Url, FOnComplete OnComplete)
{
	CancelActiveRequest();

	const int32 RequestId = ++State->NextRequestId;
	State->ActiveRequestId = RequestId;
	TSharedRef<FOnComplete> SharedOnComplete = MakeShared<FOnComplete>(MoveTemp(OnComplete));

#if WITH_DEV_AUTOMATION_TESTS
	if (GFigmaHttpClientAutomationHooks.IsValid() && GFigmaHttpClientAutomationHooks->StartRequest)
	{
		const bool bStarted = GFigmaHttpClientAutomationHooks->StartRequest(
			Url,
			Token,
			RequestId,
			[State = State, RequestId, SharedOnComplete](
				bool bConnected,
				const FString& ResponseBody,
				int32 ResponseCode)
			{
				const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
				if (State->ActiveRequestId == RequestId)
				{
					State->ActiveRequestId = 0;
				}

				if (bWasCancelled)
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
			if (State->ActiveRequestId == RequestId)
			{
				State->ActiveRequestId = 0;
			}
			if (*SharedOnComplete)
			{
				(*SharedOnComplete)(false, TEXT(""));
			}
		}
		return;
	}
#endif

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	State->ActiveRequest = Request;
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("X-Figma-Token"), Token);
	Request->SetTimeout(FigmaHttpRequestTimeoutSeconds);

	Request->OnProcessRequestComplete().BindLambda(
		[State = State, RequestId, SharedOnComplete](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bConnected)
		{
			const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
			if (State->ActiveRequestId == RequestId)
			{
				State->ActiveRequest.Reset();
				State->ActiveRequestId = 0;
			}

			if (bWasCancelled)
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
		if (State->ActiveRequestId == RequestId)
		{
			State->ActiveRequest.Reset();
			State->ActiveRequestId = 0;
		}
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaHttpClient: ProcessRequest failed"));
		if (*SharedOnComplete)
		{
			(*SharedOnComplete)(false, TEXT(""));
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void SetFigmaHttpClientAutomationHooks(const TSharedPtr<FFigmaHttpClientAutomationHooks>& InHooks)
{
	GFigmaHttpClientAutomationHooks = InHooks;
}

void ResetFigmaHttpClientAutomationHooks()
{
	GFigmaHttpClientAutomationHooks.Reset();
}
#endif

