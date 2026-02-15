// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "MCPHttpClient.h"
#include "PenToUMGLog.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopedSlowTask.h"

bool FMCPHttpClient::Connect(const FString& Endpoint)
{
	Disconnect();
	ServerEndpoint = Endpoint;

	// Step 1: POST initialize
	TSharedPtr<FJsonObject> InitParams = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("PenToUMG"));
	ClientInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	InitParams->SetObjectField(TEXT("clientInfo"), ClientInfo);
	InitParams->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	InitParams->SetObjectField(TEXT("capabilities"), Capabilities);

	FString ResponseBody;
	if (!SendRequest(TEXT("initialize"), InitParams, ResponseBody))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: initialize handshake failed"));
		return false;
	}

	if (SessionId.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: No Mcp-Session-Id in initialize response"));
		return false;
	}

	// Step 2: POST notifications/initialized
	FString NotifyBody;
	SendRequest(TEXT("notifications/initialized"), nullptr, NotifyBody, false);

	UE_LOG(LogPenToUMG, Log, TEXT("MCPHttpClient: Connected to %s (session: %s)"), *ServerEndpoint, *SessionId);
	return true;
}

void FMCPHttpClient::Disconnect()
{
	SessionId.Empty();
	ServerEndpoint.Empty();
	NextRequestId = 1;
}

bool FMCPHttpClient::SendRequest(const FString& Method,
								 const TSharedPtr<FJsonObject>& Params,
								 FString& OutResponseBody,
								 bool bExpectResponse)
{
	// 构建 JSON-RPC 2.0 请求
	TSharedRef<FJsonObject> RequestObj = MakeShared<FJsonObject>();
	RequestObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	RequestObj->SetStringField(TEXT("method"), Method);

	if (Params.IsValid())
		RequestObj->SetObjectField(TEXT("params"), Params);

	// notifications 不需要 id
	const bool bIsNotification = Method.StartsWith(TEXT("notifications/"));
	if (!bIsNotification)
		RequestObj->SetNumberField(TEXT("id"), NextRequestId++);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestObj, Writer);

	// 创建 HTTP 请求
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(ServerEndpoint);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	HttpRequest->SetContentAsString(RequestBody);

	if (!SessionId.IsEmpty())
		HttpRequest->SetHeader(TEXT("Mcp-Session-Id"), SessionId);

	// 同步等待
	bool bCompleted = false;
	bool bSuccess = false;

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[&](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (bConnectedSuccessfully && Response.IsValid())
			{
				const int32 Code = Response->GetResponseCode();
				if (Code >= 200 && Code < 300)
				{
					// 提取 Mcp-Session-Id（initialize 响应）
					const FString NewSessionId = Response->GetHeader(TEXT("Mcp-Session-Id"));
					if (!NewSessionId.IsEmpty())
						SessionId = NewSessionId;

					OutResponseBody = Response->GetContentAsString();
					bSuccess = true;
				}
				else
				{
					UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: HTTP %d for %s"), Code, *Method);
				}
			}
			else
			{
				UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: Connection failed for %s"), *Method);
			}
			bCompleted = true;
		});

	HttpRequest->ProcessRequest();

	// 阻塞等待，同时显示进度条并 tick Slate 保持编辑器 UI 响应
	const double StartTime = FPlatformTime::Seconds();
	const double Timeout = 30.0;
	const float TotalSteps = static_cast<float>(Timeout / 0.01);

	FScopedSlowTask SlowTask(TotalSteps,
		FText::FromString(FString::Printf(TEXT("PenToUMG: Waiting for MCP response (%s)..."), *Method)));
	SlowTask.MakeDialog(true);

	while (!bCompleted)
	{
		if (SlowTask.ShouldCancel())
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("MCPHttpClient: Cancelled by user for %s"), *Method);
			HttpRequest->CancelRequest();
			return false;
		}
		if (FPlatformTime::Seconds() - StartTime > Timeout)
		{
			UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: Timeout for %s"), *Method);
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

	return bSuccess;
}

bool FMCPHttpClient::ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult)
{
	// SSE 格式: "event: message\ndata: {json}\n\n"
	// 提取最后一个 "data:" 行
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
		// 可能不是 SSE 格式，直接尝试解析整个 body
		JsonLine = SSEBody;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	TSharedPtr<FJsonObject> RpcObj;
	if (!FJsonSerializer::Deserialize(Reader, RpcObj) || !RpcObj.IsValid())
		return false;

	OutResult = RpcObj->GetObjectField(TEXT("result"));
	return OutResult.IsValid();
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
		UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: Failed to parse SSE response for %s"), *ToolName);
		return false;
	}

	if (!ExtractToolResult(Result, OutResult))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("MCPHttpClient: No text content in result for %s"), *ToolName);
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

	// 构建 JSON-RPC 请求
	TSharedRef<FJsonObject> RequestObj = MakeShared<FJsonObject>();
	RequestObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	RequestObj->SetStringField(TEXT("method"), TEXT("tools/call"));
	RequestObj->SetNumberField(TEXT("id"), NextRequestId++);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), ToolName);
	if (Arguments.IsValid())
		Params->SetObjectField(TEXT("arguments"), Arguments);
	RequestObj->SetObjectField(TEXT("params"), Params);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestObj, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(ServerEndpoint);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	HttpRequest->SetHeader(TEXT("Mcp-Session-Id"), SessionId);
	HttpRequest->SetContentAsString(RequestBody);

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
				UE_LOG(LogPenToUMG, Error,
					TEXT("MCPHttpClient: Async parse failed for %s"), *ToolName);
				if (OnComplete)
					OnComplete(false, TEXT(""));
			}
		});

	HttpRequest->ProcessRequest();
}
