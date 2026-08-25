// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "MCPHttpClient.h"
#include "Design2UMGLog.h"
#include "Tests/HttpClientAutomationHooks.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FMCPHttpClientState
{
	FHttpRequestPtr ActiveRequest;
	/** Local active-request/cancel tracking id (not the JSON-RPC id). */
	int32 ActiveTrackingRequestId = 0;
	/** Monotonic across Disconnect(): late old-session callbacks can never collide with new-session ids. */
	int32 NextTrackingRequestId = 0;
	/** Cancel marks persist until their (possibly late) callback consumes them. */
	TSet<int32> CancelledRequestIds;
};

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	TSharedPtr<FMCPHttpClientAutomationHooks> GMCPHttpClientAutomationHooks;
}
#endif

namespace
{
	TSharedPtr<FJsonObject> BuildInitializeParams()
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
		ClientInfo->SetStringField(TEXT("name"), TEXT("Design2UMG"));
		ClientInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
		Params->SetObjectField(TEXT("clientInfo"), ClientInfo);
		Params->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));
		Params->SetObjectField(TEXT("capabilities"), MakeShared<FJsonObject>());
		return Params;
	}

	FString BuildRpcRequestBody(const FString& Method, const TSharedPtr<FJsonObject>& Params, int32* InOutRequestId)
	{
		TSharedRef<FJsonObject> RequestObj = MakeShared<FJsonObject>();
		RequestObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		RequestObj->SetStringField(TEXT("method"), Method);

		if (Params.IsValid())
		{
			RequestObj->SetObjectField(TEXT("params"), Params);
		}

		if (!Method.StartsWith(TEXT("notifications/")) && InOutRequestId)
		{
			RequestObj->SetNumberField(TEXT("id"), (*InOutRequestId)++);
		}

		FString RequestBody;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
		FJsonSerializer::Serialize(RequestObj, Writer);
		return RequestBody;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreatePostRequest(
		const FString& Endpoint,
		const FString& SessionId,
		const FString& RequestBody)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(Endpoint);
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
		if (!SessionId.IsEmpty())
		{
			HttpRequest->SetHeader(TEXT("Mcp-Session-Id"), SessionId);
		}
		HttpRequest->SetContentAsString(RequestBody);
		HttpRequest->SetTimeout(30.0f);
		return HttpRequest;
	}

}

FMCPHttpClient::FMCPHttpClient()
	: State(MakeShared<FMCPHttpClientState, ESPMode::ThreadSafe>())
{
}

FMCPHttpClient::~FMCPHttpClient()
{
	CancelRequests();
}

void FMCPHttpClient::ConnectAsync(const FString& Endpoint, TFunction<void(bool bSuccess)> OnComplete)
{
	Disconnect();
	ServerEndpoint = Endpoint;

	SendRequestAsync(TEXT("initialize"), BuildInitializeParams(), true,
		[this, OnComplete](bool bInitOk, const FString&, const FString& NewSessionId)
	{
		if (!bInitOk)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: async initialize handshake failed"));
			if (OnComplete)
				OnComplete(false);
			return;
		}

		if (NewSessionId.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: No Mcp-Session-Id in async initialize response"));
			if (OnComplete)
				OnComplete(false);
			return;
		}

		SessionId = NewSessionId;

		SendRequestAsync(TEXT("notifications/initialized"), nullptr, false,
			[this, OnComplete](bool bNotifyOk, const FString&, const FString&)
		{
			if (!bNotifyOk)
				UE_LOG(LogDesign2UMG, Warning, TEXT("MCPHttpClient: notifications/initialized failed after async initialize"));

			UE_LOG(LogDesign2UMG, Log, TEXT("MCPHttpClient: Connected to %s"), *ServerEndpoint);
			if (OnComplete)
				OnComplete(true);
		});
	});
}

void FMCPHttpClient::Disconnect()
{
	CancelRequests();
	SessionId.Empty();
	ServerEndpoint.Empty();
	// JSON-RPC ids are per-session protocol state and restart cleanly.
	// Tracking ids intentionally stay MONOTONIC across sessions: a late-arriving
	// callback from an old session can then never collide with a new session's
	// request id, and its cancellation mark stays in the set until that late
	// callback consumes it, so stale responses are always recognized as
	// cancelled instead of being applied to the new session.
	NextRpcRequestId = 1;
}

void FMCPHttpClient::CancelActiveRequest()
{
	if (State->ActiveRequest.IsValid())
	{
		State->CancelledRequestIds.Add(State->ActiveTrackingRequestId);
		State->ActiveRequest->CancelRequest();
		State->ActiveRequest.Reset();
		State->ActiveTrackingRequestId = 0;
		return;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (GMCPHttpClientAutomationHooks.IsValid() && State->ActiveTrackingRequestId != 0)
	{
		State->CancelledRequestIds.Add(State->ActiveTrackingRequestId);
		if (GMCPHttpClientAutomationHooks->CancelRequest)
		{
			GMCPHttpClientAutomationHooks->CancelRequest(State->ActiveTrackingRequestId);
		}
		State->ActiveTrackingRequestId = 0;
	}
#endif
}

void FMCPHttpClient::SendRequestAsync(
	const FString& Method,
	const TSharedPtr<FJsonObject>& Params,
	bool bExpectResponse,
	TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete)
{
	CancelActiveRequest();

	const FString RequestBody = BuildRpcRequestBody(Method, Params, &NextRpcRequestId);
	const int32 RequestId = ++State->NextTrackingRequestId;
	State->ActiveTrackingRequestId = RequestId;

#if WITH_DEV_AUTOMATION_TESTS
	if (GMCPHttpClientAutomationHooks.IsValid() && GMCPHttpClientAutomationHooks->StartAsyncRequest)
	{
		const bool bStarted = GMCPHttpClientAutomationHooks->StartAsyncRequest(
			Method,
			RequestBody,
			RequestId,
			[State = State, RequestId, OnComplete, Method, bExpectResponse](
				bool bConnectedSuccessfully,
				const FString& ResponseBody,
				int32 ResponseCode,
				const FString& NewSessionId)
			{
				const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
				if (State->ActiveTrackingRequestId == RequestId)
				{
					State->ActiveTrackingRequestId = 0;
				}

				if (bWasCancelled)
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *Method);
					return;
				}

				if (!bConnectedSuccessfully)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *Method);
					if (OnComplete)
					{
						OnComplete(false, TEXT(""), TEXT(""));
					}
					return;
				}

				if (ResponseCode < 200 || ResponseCode >= 300)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), ResponseCode, *Method);
					if (OnComplete)
					{
						OnComplete(false, TEXT(""), TEXT(""));
					}
					return;
				}

				if (OnComplete)
				{
					OnComplete(true, bExpectResponse ? ResponseBody : FString(), NewSessionId);
				}
			});

		if (!bStarted)
		{
			if (State->ActiveTrackingRequestId == RequestId)
			{
				State->ActiveTrackingRequestId = 0;
			}
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *Method);
			if (OnComplete)
			{
				OnComplete(false, TEXT(""), TEXT(""));
			}
		}
		return;
	}
#endif

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);
	State->ActiveRequest = HttpRequest;
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State = State, RequestId, OnComplete, Method, bExpectResponse](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
		if (State->ActiveTrackingRequestId == RequestId)
		{
			State->ActiveRequest.Reset();
			State->ActiveTrackingRequestId = 0;
		}

		if (bWasCancelled)
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *Method);
			return;
		}

		if (!bConnectedSuccessfully || !Response.IsValid())
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *Method);
			if (OnComplete)
				OnComplete(false, TEXT(""), TEXT(""));
			return;
		}

		const int32 Code = Response->GetResponseCode();
		if (Code < 200 || Code >= 300)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), Code, *Method);
			if (OnComplete)
				OnComplete(false, TEXT(""), TEXT(""));
			return;
		}

		const FString ResponseBody = bExpectResponse ? Response->GetContentAsString() : FString();
		const FString NewSessionId = Response->GetHeader(TEXT("Mcp-Session-Id"));
		if (OnComplete)
			OnComplete(true, ResponseBody, NewSessionId);
	});

	if (!HttpRequest->ProcessRequest())
	{
		if (State->ActiveTrackingRequestId == RequestId)
		{
			State->ActiveRequest.Reset();
			State->ActiveTrackingRequestId = 0;
		}
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *Method);
		if (OnComplete)
			OnComplete(false, TEXT(""), TEXT(""));
	}
}

bool FMCPHttpClient::ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult)
{
	// SSE format: "event: message\ndata: {json}\n\n"
	// Extract the last "data:" line from the SSE payload.
	FString JsonLine;
	TArray<FString> Lines;
	SSEBody.ParseIntoArrayLines(Lines);

	for (int32 i = Lines.Num() - 1; i >= 0; --i)
	{
		if (Lines[i].StartsWith(TEXT("data:")))
		{
			JsonLine = Lines[i].RightChop(5).TrimStart();
			break;
		}
	}

	if (JsonLine.IsEmpty())
	{
		// This may not be SSE format, so try parsing the whole body directly.
		JsonLine = SSEBody;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	TSharedPtr<FJsonObject> RpcObj;
	if (!FJsonSerializer::Deserialize(Reader, RpcObj) || !RpcObj.IsValid())
		return false;

	OutResult = RpcObj->GetObjectField(TEXT("result"));
	if (!OutResult.IsValid())
	{
		const TSharedPtr<FJsonObject>* ErrObj = nullptr;
		if (RpcObj->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj)
		{
			int32 Code = (*ErrObj)->GetIntegerField(TEXT("code"));
			FString Msg = (*ErrObj)->GetStringField(TEXT("message"));
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: JSON-RPC error %d: %s"), Code, *Msg);
		}
		return false;
	}
	return true;
}

bool FMCPHttpClient::ExtractToolResult(const TSharedPtr<FJsonObject>& Result, FString& OutText)
{
	const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
	if (!Result->TryGetArrayField(TEXT("content"), ContentArr) || !ContentArr || ContentArr->Num() == 0)
		return false;

	const TSharedPtr<FJsonObject>& First = (*ContentArr)[0]->AsObject();
	if (!First.IsValid())
		return false;

	return First->TryGetStringField(TEXT("text"), OutText);
}

void FMCPHttpClient::CallToolAsync(const FString& ToolName,
								   const TSharedPtr<FJsonObject>& Arguments,
								   TFunction<void(bool bSuccess, const FString& Result)> OnComplete)
{
	CancelActiveRequest();

	if (!IsConnected())
	{
		if (OnComplete)
			OnComplete(false, TEXT(""));
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), ToolName);
	if (Arguments.IsValid())
		Params->SetObjectField(TEXT("arguments"), Arguments);

	const FString RequestBody = BuildRpcRequestBody(TEXT("tools/call"), Params, &NextRpcRequestId);
	const int32 RequestId = ++State->NextTrackingRequestId;
	State->ActiveTrackingRequestId = RequestId;

#if WITH_DEV_AUTOMATION_TESTS
	if (GMCPHttpClientAutomationHooks.IsValid() && GMCPHttpClientAutomationHooks->StartAsyncRequest)
	{
		const bool bStarted = GMCPHttpClientAutomationHooks->StartAsyncRequest(
			TEXT("tools/call"),
			RequestBody,
			RequestId,
			[State = State, RequestId, OnComplete, ToolName](
				bool bConnected,
				const FString& Body,
				int32 ResponseCode,
				const FString&)
			{
				const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
				if (State->ActiveTrackingRequestId == RequestId)
				{
					State->ActiveTrackingRequestId = 0;
				}

				if (bWasCancelled)
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *ToolName);
					return;
				}

				if (!bConnected || ResponseCode < 200 || ResponseCode >= 300)
				{
					if (OnComplete)
					{
						OnComplete(false, TEXT(""));
					}
					return;
				}

				TSharedPtr<FJsonObject> Result;
				FString Text;
				if (ParseSSEResponse(Body, Result) && ExtractToolResult(Result, Text))
				{
					if (OnComplete)
					{
						OnComplete(true, Text);
					}
				}
				else
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Async parse failed for %s"), *ToolName);
					if (OnComplete)
					{
						OnComplete(false, TEXT(""));
					}
				}
			});

		if (!bStarted)
		{
			if (State->ActiveTrackingRequestId == RequestId)
			{
				State->ActiveTrackingRequestId = 0;
			}
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *ToolName);
			if (OnComplete)
			{
				OnComplete(false, TEXT(""));
			}
		}
		return;
	}
#endif

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);
	State->ActiveRequest = HttpRequest;
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State = State, RequestId, OnComplete, ToolName](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const bool bWasCancelled = State->CancelledRequestIds.Remove(RequestId) > 0;
			if (State->ActiveTrackingRequestId == RequestId)
			{
				State->ActiveRequest.Reset();
				State->ActiveTrackingRequestId = 0;
			}

			if (bWasCancelled)
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *ToolName);
				return;
			}

			if (!bConnected || !Response.IsValid()
				|| Response->GetResponseCode() < 200
				|| Response->GetResponseCode() >= 300)
			{
				if (OnComplete)
					OnComplete(false, TEXT(""));
				return;
			}

			const FString Body = Response->GetContentAsString();
			TSharedPtr<FJsonObject> Result;
			FString Text;

			if (ParseSSEResponse(Body, Result) && ExtractToolResult(Result, Text))
			{
				if (OnComplete)
					OnComplete(true, Text);
			}
			else
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("MCPHttpClient: Async parse failed for %s"), *ToolName);
				if (OnComplete)
					OnComplete(false, TEXT(""));
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		if (State->ActiveTrackingRequestId == RequestId)
		{
			State->ActiveRequest.Reset();
			State->ActiveTrackingRequestId = 0;
		}
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *ToolName);
		if (OnComplete)
			OnComplete(false, TEXT(""));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void SetMCPHttpClientAutomationHooks(const TSharedPtr<FMCPHttpClientAutomationHooks>& InHooks)
{
	GMCPHttpClientAutomationHooks = InHooks;
}

void ResetMCPHttpClientAutomationHooks()
{
	GMCPHttpClientAutomationHooks.Reset();
}
#endif
