// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "MCPHttpClient.h"
#include "Design2UMGLog.h"
#include "DesignHttpClientSlot.h"
#include "DesignJsonUtils.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// Per JSON-RPC request timeout. No derivation on record; fixed when the MCP
	// transport was introduced. FigmaHttpClient intentionally raises its
	// document fetch above this baseline (see FigmaHttpRequestTimeoutSeconds).
	constexpr float McpHttpRequestTimeoutSeconds = 30.0f;

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
		HttpRequest->SetTimeout(McpHttpRequestTimeoutSeconds);
		return HttpRequest;
	}

}

FMCPHttpClient::FMCPHttpClient()
	: State(MakeShared<FDesignHttpClientSlot, ESPMode::ThreadSafe>())
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
	// Disconnect stays silent about the in-flight request: the client object
	// is being torn down or reconnected, so no failure is owed here.
	InFlightCancelState.Reset();
	InFlightCancelFailure = nullptr;
	SessionId.Empty();
	ServerEndpoint.Empty();
	// JSON-RPC ids only need to be unique within one connection, so each
	// connection gets its own id space and this counter restarts from 1 here.
	// This is separate from the slot's tracking ids (FDesignHttpClientSlot),
	// which do stay monotonic across sessions: a late-arriving callback from an
	// old session can then never collide with a new session's tracking id, and
	// the slot's cancellation watermark keeps recognizing it as cancelled, so
	// stale responses are never applied to the new session.
	NextRpcRequestId = 1;
}

void FMCPHttpClient::CancelActiveRequest()
{
	State->CancelActiveRequest();
}

void FMCPHttpClient::SendRequestAsync(
	const FString& Method,
	const TSharedPtr<FJsonObject>& Params,
	bool bExpectResponse,
	TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete,
	const FString& InLogLabel,
	bool bLogTransportErrors)
{
	// A superseded in-flight request would otherwise never fire its callback;
	// deliver the same failure shape the HTTP failure paths use (false with
	// empty strings) exactly once before starting the new request.
	if (InFlightCancelState.IsValid() && !InFlightCancelState->bDelivered && InFlightCancelFailure)
	{
		TFunction<void()> CancelFailure = MoveTemp(InFlightCancelFailure);
		CancelFailure();
	}
	InFlightCancelState.Reset();
	InFlightCancelFailure = nullptr;
	CancelActiveRequest();

	const FString LogLabel = InLogLabel.IsEmpty() ? Method : InLogLabel;
	const FString RequestBody = BuildRpcRequestBody(Method, Params, &NextRpcRequestId);
	const int32 RequestId = State->BeginRequest();

	// Failure delivery owned by this request while it is in flight.
	InFlightCancelState = MakeShared<FInFlightCancelState, ESPMode::ThreadSafe>();
	TSharedPtr<FInFlightCancelState, ESPMode::ThreadSafe> CancelState = InFlightCancelState;
	const FString CancelLogLabel = LogLabel;
	InFlightCancelFailure = [CancelState, OnComplete, CancelLogLabel]()
	{
		CancelState->bDelivered = true;
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("MCPHttpClient: Request cancelled before completion for %s"),
			*CancelLogLabel);
		if (OnComplete)
		{
			OnComplete(false, TEXT(""), TEXT(""));
		}
	};

#if WITH_DEV_AUTOMATION_TESTS
	if (DesignHttpClientSlot::HasAutomationTransport())
	{
		const bool bStarted = DesignHttpClientSlot::StartAutomationTransport(
			RequestId,
			[State = State, RequestId, OnComplete, LogLabel, bExpectResponse, bLogTransportErrors, CancelState](
				bool bConnectedSuccessfully,
				const FString& ResponseBody,
				int32 ResponseCode,
				const FString& NewSessionId)
		{
			if (!State->ConsumeCompletion(RequestId))
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *LogLabel);
				return;
			}

			CancelState->bDelivered = true;

			if (!bConnectedSuccessfully)
			{
				if (bLogTransportErrors)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *LogLabel);
				}
				if (OnComplete)
				{
					OnComplete(false, TEXT(""), TEXT(""));
				}
				return;
			}

			if (ResponseCode < 200 || ResponseCode >= 300)
			{
				if (bLogTransportErrors)
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), ResponseCode, *LogLabel);
				}
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
			State->ClearActiveRequest(RequestId);
			CancelState->bDelivered = true;
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *LogLabel);
			if (OnComplete)
			{
				OnComplete(false, TEXT(""), TEXT(""));
			}
		}
		return;
	}
#endif

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);
	State->BindActiveRequest(HttpRequest);
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State = State, RequestId, OnComplete, LogLabel, bExpectResponse, bLogTransportErrors, CancelState](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!State->ConsumeCompletion(RequestId))
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("MCPHttpClient: Request cancelled for %s"), *LogLabel);
			return;
		}

		CancelState->bDelivered = true;

		if (!bConnectedSuccessfully || !Response.IsValid())
		{
			if (bLogTransportErrors)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *LogLabel);
			}
			if (OnComplete)
				OnComplete(false, TEXT(""), TEXT(""));
			return;
		}

		const int32 Code = Response->GetResponseCode();
		if (Code < 200 || Code >= 300)
		{
			if (bLogTransportErrors)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), Code, *LogLabel);
			}
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
		State->ClearActiveRequest(RequestId);
		CancelState->bDelivered = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *LogLabel);
		if (OnComplete)
			OnComplete(false, TEXT(""), TEXT(""));
	}
}

bool FMCPHttpClient::ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult)
{
	// SSE format: "event: message\ndata: {json}\n\n". Events are separated by
	// blank lines; multiple "data:" lines inside one event are joined with
	// newlines per the SSE spec. We keep the payload of the last event that
	// actually carried data.
	FString JsonPayload;
	TArray<FString> Lines;
	SSEBody.ParseIntoArrayLines(Lines);

	TArray<FString> DataLines;
	const auto FlushEvent = [&JsonPayload, &DataLines]()
	{
		if (DataLines.Num() > 0)
		{
			JsonPayload = FString::Join(DataLines, TEXT("\n"));
			DataLines.Reset();
		}
	};
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty())
		{
			FlushEvent();
		}
		else if (Line.StartsWith(TEXT("data:")))
		{
			DataLines.Add(Line.RightChop(5).TrimStart());
		}
	}
	FlushEvent();

	if (JsonPayload.IsEmpty())
	{
		// This may not be SSE format, so try parsing the whole body directly.
		JsonPayload = SSEBody;
	}

	TSharedPtr<FJsonObject> RpcObj;
	if (!DesignJsonUtils::TryParseObject(JsonPayload, RpcObj))
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

	// Transport failures are reported through OnComplete only: the original
	// tools/call path never logged connection/HTTP errors itself, so it passes
	// bLogTransportErrors=false and keeps its own parse-failure log below.
	SendRequestAsync(TEXT("tools/call"), Params, true,
		[OnComplete, ToolName](bool bSuccess, const FString& ResponseBody, const FString&)
		{
			if (!bSuccess)
			{
				if (OnComplete)
					OnComplete(false, TEXT(""));
				return;
			}

			TSharedPtr<FJsonObject> Result;
			FString Text;
			if (ParseSSEResponse(ResponseBody, Result) && ExtractToolResult(Result, Text))
			{
				if (OnComplete)
					OnComplete(true, Text);
			}
			else
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Async parse failed for %s"), *ToolName);
				if (OnComplete)
					OnComplete(false, TEXT(""));
			}
		},
		ToolName,
		/*bLogTransportErrors=*/false);
}
