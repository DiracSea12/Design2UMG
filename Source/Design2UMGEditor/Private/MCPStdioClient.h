// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** Owns a local MCP server and its newline-delimited JSON-RPC connection. */
class DESIGN2UMGEDITOR_API FMCPStdioClient
{
public:
	FMCPStdioClient() = default;
	~FMCPStdioClient();
	FMCPStdioClient(const FMCPStdioClient&) = delete;
	FMCPStdioClient& operator=(const FMCPStdioClient&) = delete;

	void ConnectAsync(const FString& Executable, const FString& Arguments, TFunction<void(bool)> OnComplete);
	void Disconnect();
	void CallToolAsync(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
		TFunction<void(bool, const FString&)> OnComplete);
	/** Cancellation and disconnect silently discard pending callbacks. */
	void CancelActiveRequest();
	void CancelRequests() { CancelActiveRequest(); }
	bool IsConnected() const;
	bool IsRunning() const;

private:
	struct FSession;
	TSharedPtr<FSession> Session;
	static void SendRequest(TSharedPtr<FSession> Connection, const FString& Method,
		const TSharedPtr<FJsonObject>& Params, TFunction<void(bool, const TSharedPtr<FJsonObject>&)> OnComplete);
};
