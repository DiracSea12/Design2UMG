// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "MCPHttpClient.h"
#include "Design2UMGLog.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopedSlowTask.h"

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

bool FMCPHttpClient::Connect(const FString& Endpoint)
{
	Disconnect();
	ServerEndpoint = Endpoint;

	// Step 1: POST initialize
	FString ResponseBody;
	if (!SendRequest(TEXT("initialize"), BuildInitializeParams(), ResponseBody))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: initialize handshake failed"));
		return false;
	}

	if (SessionId.IsEmpty())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: No Mcp-Session-Id in initialize response"));
		return false;
	}

	// Step 2: POST notifications/initialized
	FString NotifyBody;
	SendRequest(TEXT("notifications/initialized"), nullptr, NotifyBody, false);

	UE_LOG(LogDesign2UMG, Log, TEXT("MCPHttpClient: Connected to %s (session: %s)"), *ServerEndpoint, *SessionId);
	return true;
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

			UE_LOG(LogDesign2UMG, Log, TEXT("MCPHttpClient: Connected to %s (session: %s)"), *ServerEndpoint, *SessionId);
			if (OnComplete)
				OnComplete(true);
		});
	});
}

void FMCPHttpClient::Disconnect()
{
	SessionId.Empty();
	ServerEndpoint.Empty();
	NextRequestId = 1;
}

void FMCPHttpClient::SendRequestAsync(
	const FString& Method,
	const TSharedPtr<FJsonObject>& Params,
	bool bExpectResponse,
	TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete)
{
	const FString RequestBody = BuildRpcRequestBody(Method, Params, &NextRequestId);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[OnComplete, Method, bExpectResponse](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
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
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *Method);
		if (OnComplete)
			OnComplete(false, TEXT(""), TEXT(""));
	}
}

bool FMCPHttpClient::SendRequest(const FString& Method,
								 const TSharedPtr<FJsonObject>& Params,
								 FString& OutResponseBody,
								 bool bExpectResponse)
{
	const FString RequestBody = BuildRpcRequestBody(Method, Params, &NextRequestId);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);

	// Keep request state on the heap so timeout/cancel cannot leave a dangling stack capture.
	struct FRequestState
	{
		FThreadSafeBool bCompleted{false};
		bool bSuccess = false;
		FString ResponseBody;
		FString NewSessionId;
	};
	TSharedRef<FRequestState> State = MakeShared<FRequestState>();

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State, Method](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (bConnectedSuccessfully && Response.IsValid())
			{
				const int32 Code = Response->GetResponseCode();
				if (Code >= 200 && Code < 300)
				{
					State->NewSessionId = Response->GetHeader(TEXT("Mcp-Session-Id"));
					State->ResponseBody = Response->GetContentAsString();
					State->bSuccess = true;
				}
				else
				{
					UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), Code, *Method);
				}
			}
			else
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *Method);
			}
			State->bCompleted = true;
		});

	if (!HttpRequest->ProcessRequest())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *Method);
		return false;
	}

	// Wait synchronously while ticking Slate through the progress UI to keep the editor responsive.
	const double StartTime = FPlatformTime::Seconds();
	const double Timeout = 30.0;
	const float TotalSteps = static_cast<float>(Timeout / 0.01);

	FScopedSlowTask SlowTask(TotalSteps,
		FText::FromString(FString::Printf(TEXT("Design2UMG: Waiting for MCP response (%s)..."), *Method)));
	SlowTask.MakeDialog(true);

	while (!State->bCompleted)
	{
		if (SlowTask.ShouldCancel())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("MCPHttpClient: Cancelled by user for %s"), *Method);
			HttpRequest->CancelRequest();
			return false;
		}
		if (FPlatformTime::Seconds() - StartTime > Timeout)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Timeout for %s"), *Method);
			HttpRequest->CancelRequest();
			return false;
		}
		FHttpModule::Get().GetHttpManager().Tick(0.01f);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick();
		}
		SlowTask.EnterProgressFrame(1.0f);
		FPlatformProcess::Sleep(0.01f);
	}

	if (!State->NewSessionId.IsEmpty())
		SessionId = State->NewSessionId;
	OutResponseBody = MoveTemp(State->ResponseBody);
	return State->bSuccess;
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

bool FMCPHttpClient::CallTool(const FString& ToolName,
							  const TSharedPtr<FJsonObject>& Arguments,
							  FString& OutResult)
{
	if (!IsConnected())
		return false;

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), ToolName);
	if (Arguments.IsValid())
		Params->SetObjectField(TEXT("arguments"), Arguments);

	FString ResponseBody;
	if (!SendRequest(TEXT("tools/call"), Params, ResponseBody))
		return false;

	TSharedPtr<FJsonObject> Result;
	if (!ParseSSEResponse(ResponseBody, Result))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: Failed to parse SSE response for %s"), *ToolName);
		return false;
	}

	if (!ExtractToolResult(Result, OutResult))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: No text content in result for %s"), *ToolName);
		return false;
	}

	return true;
}

void FMCPHttpClient::CallToolAsync(const FString& ToolName,
								   const TSharedPtr<FJsonObject>& Arguments,
								   TFunction<void(bool bSuccess, const FString& Result)> OnComplete)
{
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

	const FString RequestBody = BuildRpcRequestBody(TEXT("tools/call"), Params, &NextRequestId);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = CreatePostRequest(ServerEndpoint, SessionId, RequestBody);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[OnComplete, ToolName](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
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
		UE_LOG(LogDesign2UMG, Error, TEXT("MCPHttpClient: ProcessRequest failed for %s"), *ToolName);
		if (OnComplete)
			OnComplete(false, TEXT(""));
	}
}
