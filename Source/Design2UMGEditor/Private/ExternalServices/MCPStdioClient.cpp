// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "MCPStdioClient.h"
#include "Containers/Ticker.h"
#include "Containers/StringConv.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr double RequestTimeoutSeconds = 30.0;

	FString RpcMessage(const FString& Method, const TSharedPtr<FJsonObject>& Params, int32 Id = 0)
	{
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Message->SetStringField(TEXT("method"), Method);
		if (Params) Message->SetObjectField(TEXT("params"), Params);
		if (Id) Message->SetNumberField(TEXT("id"), Id);
		FString Json;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Message, Writer);
		return Json;
	}

	bool WriteRpcMessage(void* Pipe, const FString& Json)
	{
		const FString Message = Json + TEXT("\n");
		const FTCHARToUTF8 Encoded(*Message);
		int32 BytesWritten = 0;
		return FPlatformProcess::WritePipe(Pipe, reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length(), &BytesWritten)
			&& BytesWritten == Encoded.Length();
	}
}

struct FMCPStdioClient::FSession
{
	FProcHandle Process;
	void* StdoutRead = nullptr;
	void* StdinWrite = nullptr;
	void* StderrRead = nullptr;
	FTSTicker::FDelegateHandle Ticker;
	TArray<uint8> BufferedOutput;
	int32 NextId = 1;
	int32 PendingId = 0;
	double Deadline = 0;
	bool bConnected = false;
	bool bStopped = false;
	TFunction<void(bool, const TSharedPtr<FJsonObject>&)> Pending;

	~FSession() { Stop(); }

	void Stop()
	{
		bStopped = true;
		bConnected = false;
		PendingId = 0;
		Pending = nullptr;
		if (Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
			Ticker.Reset();
		}
		if (Process.IsValid())
		{
			if (FPlatformProcess::IsProcRunning(Process)) FPlatformProcess::TerminateProc(Process);
			FPlatformProcess::CloseProc(Process);
		}
		FPlatformProcess::ClosePipe(StdoutRead, StdinWrite);
		FPlatformProcess::ClosePipe(StderrRead, nullptr);
		StdoutRead = StdinWrite = StderrRead = nullptr;
		BufferedOutput.Reset();
	}

	void Complete(bool bSuccess, const TSharedPtr<FJsonObject>& Result = nullptr)
	{
		auto Callback = MoveTemp(Pending);
		PendingId = 0;
		if (Callback) Callback(bSuccess, Result);
	}

	bool Poll()
	{
		TArray<uint8> Bytes;
		FPlatformProcess::ReadPipeToArray(StderrRead, Bytes);
		if (FPlatformProcess::ReadPipeToArray(StdoutRead, Bytes)) BufferedOutput.Append(Bytes);
		int32 Newline = INDEX_NONE;
		while (!bStopped && BufferedOutput.Find(static_cast<uint8>('\n'), Newline))
		{
			const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(BufferedOutput.GetData()), Newline);
			const FString Line(Decoded.Length(), Decoded.Get());
			BufferedOutput.RemoveAt(0, Newline + 1, EAllowShrinking::No);
			TSharedPtr<FJsonObject> Message;
			if (!DesignJsonUtils::TryParseObject(Line, Message))
			{
				auto Callback = MoveTemp(Pending);
				Stop();
				if (Callback) Callback(false, nullptr);
				break;
			}
			double Id = 0;
			if (!Message->TryGetNumberField(TEXT("id"), Id) || !PendingId || Id != PendingId) continue;
			const TSharedPtr<FJsonObject>* Result = nullptr;
			const bool bSuccess = Message->TryGetObjectField(TEXT("result"), Result) && Result && Result->IsValid();
			Complete(bSuccess, bSuccess ? *Result : nullptr);
		}
		if (bStopped) return false;
		if (!FPlatformProcess::IsProcRunning(Process))
		{
			auto Callback = MoveTemp(Pending);
			Stop();
			if (Callback) Callback(false, nullptr);
			return false;
		}
		if (PendingId && FPlatformTime::Seconds() >= Deadline) Complete(false);
		return !bStopped;
	}
};

FMCPStdioClient::~FMCPStdioClient() { Disconnect(); }

void FMCPStdioClient::Disconnect()
{
	if (Session) Session->Stop();
	Session.Reset();
}

bool FMCPStdioClient::IsConnected() const { return Session && Session->bConnected; }
bool FMCPStdioClient::IsRunning() const
{
	return Session && !Session->bStopped && Session->Process.IsValid() && FPlatformProcess::IsProcRunning(Session->Process);
}

void FMCPStdioClient::CancelActiveRequest()
{
	if (Session)
	{
		Session->PendingId = 0;
		Session->Pending = nullptr;
	}
}

void FMCPStdioClient::SendRequest(TSharedPtr<FSession> Connection, const FString& Method,
	const TSharedPtr<FJsonObject>& Params, TFunction<void(bool, const TSharedPtr<FJsonObject>&)> OnComplete)
{
	auto Previous = MoveTemp(Connection->Pending);
	Connection->PendingId = 0;
	if (Previous) Previous(false, nullptr);
	// A superseded callback may disconnect or start its own request.
	if (Connection->bStopped || Connection->PendingId)
	{
		if (OnComplete) OnComplete(false, nullptr);
		return;
	}
	Connection->PendingId = Connection->NextId++;
	Connection->Deadline = FPlatformTime::Seconds() + RequestTimeoutSeconds;
	Connection->Pending = MoveTemp(OnComplete);
	if (!WriteRpcMessage(Connection->StdinWrite, RpcMessage(Method, Params, Connection->PendingId)))
		Connection->Complete(false);
}

void FMCPStdioClient::ConnectAsync(const FString& Executable, const FString& Arguments, TFunction<void(bool)> OnComplete)
{
	Disconnect();
	Session = MakeShared<FSession>();
	const auto Connection = Session;
	void* StdoutWrite = nullptr;
	void* StdinRead = nullptr;
	void* StderrWrite = nullptr;
	const bool bPipesReady = FPlatformProcess::CreatePipe(Connection->StdoutRead, StdoutWrite)
		&& FPlatformProcess::CreatePipe(StdinRead, Connection->StdinWrite, true)
		&& FPlatformProcess::CreatePipe(Connection->StderrRead, StderrWrite);
	if (bPipesReady)
		Connection->Process = FPlatformProcess::CreateProc(*Executable, *Arguments, false, true, true, nullptr, 0, nullptr,
			StdoutWrite, StdinRead, StderrWrite);
	FPlatformProcess::ClosePipe(StdinRead, StdoutWrite);
	FPlatformProcess::ClosePipe(nullptr, StderrWrite);
	if (!Connection->Process.IsValid())
	{
		Connection->Stop();
		if (OnComplete) OnComplete(false);
		return;
	}
	const TWeakPtr<FSession> WeakConnection = Connection;
	Connection->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakConnection](float)
	{
		const auto Pinned = WeakConnection.Pin();
		return Pinned && Pinned->Poll();
	}));
	const TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("Design2UMG"));
	ClientInfo->SetStringField(TEXT("version"), TEXT("1.1.0"));
	Params->SetObjectField(TEXT("clientInfo"), ClientInfo);
	Params->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));
	Params->SetObjectField(TEXT("capabilities"), MakeShared<FJsonObject>());
	SendRequest(Connection, TEXT("initialize"), Params, [WeakConnection, OnComplete = MoveTemp(OnComplete)](bool bSuccess, const TSharedPtr<FJsonObject>& Result)
	{
		const auto Pinned = WeakConnection.Pin();
		if (!Pinned) return;
		FString ProtocolVersion;
		const TSharedPtr<FJsonObject>* Capabilities = nullptr;
		bSuccess = bSuccess && Result && Result->TryGetStringField(TEXT("protocolVersion"), ProtocolVersion)
			&& ProtocolVersion == TEXT("2024-11-05") && Result->TryGetObjectField(TEXT("capabilities"), Capabilities);
		if (bSuccess) bSuccess = WriteRpcMessage(Pinned->StdinWrite, RpcMessage(TEXT("notifications/initialized"), nullptr));
		Pinned->bConnected = bSuccess;
		if (!bSuccess) Pinned->Stop();
		if (OnComplete) OnComplete(bSuccess);
	});
}

void FMCPStdioClient::CallToolAsync(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
	TFunction<void(bool, const FString&)> OnComplete)
{
	if (!IsConnected())
	{
		if (OnComplete) OnComplete(false, FString());
		return;
	}
	const TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("name"), ToolName);
	Params->SetObjectField(TEXT("arguments"), Arguments ? Arguments : MakeShared<FJsonObject>());
	SendRequest(Session, TEXT("tools/call"), Params, [OnComplete = MoveTemp(OnComplete)](bool bSuccess, const TSharedPtr<FJsonObject>& Result)
	{
		FString Text;
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		bool bIsError = false;
		if (Result) Result->TryGetBoolField(TEXT("isError"), bIsError);
		bSuccess = bSuccess && Result && Result->TryGetArrayField(TEXT("content"), Content) && Content;
		if (bSuccess)
		{
			TArray<FString> Parts;
			for (const auto& Item : *Content)
			{
				const auto Object = Item.IsValid() && Item->Type == EJson::Object ? Item->AsObject() : nullptr;
				FString Type, Part;
				if (Object && Object->TryGetStringField(TEXT("type"), Type) && Type == TEXT("text") && Object->TryGetStringField(TEXT("text"), Part)) Parts.Add(Part);
			}
			bSuccess = !Parts.IsEmpty() && !bIsError;
			Text = FString::Join(Parts, TEXT("\n"));
		}
		if (OnComplete) OnComplete(bSuccess, Text);
	});
}
