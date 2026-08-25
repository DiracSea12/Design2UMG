// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Streamable MCP HTTP client implementing JSON-RPC 2.0 with Mcp-Session-Id.
 * Responses are received through SSE (text/event-stream).
 */
class DESIGN2UMGEDITOR_API FMCPHttpClient
{
public:
	FMCPHttpClient();
	~FMCPHttpClient();

	FMCPHttpClient(const FMCPHttpClient&) = delete;
	FMCPHttpClient& operator=(const FMCPHttpClient&) = delete;

	/**
	 * Connect to the MCP server and complete the handshake.
	 * Internally this sends initialize, extracts Mcp-Session-Id, then sends
	 * notifications/initialized. IsConnected() returns true on success.
	 */
	void ConnectAsync(const FString& Endpoint, TFunction<void(bool bSuccess)> OnComplete);

	/** Disconnect the current session. */
	void Disconnect();

	/**
	 * Call an MCP tool asynchronously.
	 * The callback is invoked on the game thread with the decoded
	 * result.content[0].text payload.
	 * Concurrent calls are not supported.
	 */
	void CallToolAsync(const FString& ToolName,
					   const TSharedPtr<FJsonObject>& Arguments,
					   TFunction<void(bool bSuccess, const FString& Result)> OnComplete);

	/** Cancel the currently active asynchronous MCP HTTP request, if any. */
	void CancelActiveRequest();
	void CancelRequests() { CancelActiveRequest(); }

	bool IsConnected() const { return !SessionId.IsEmpty(); }

private:
	FString ServerEndpoint;
	FString SessionId;
	/** JSON-RPC id embedded in outgoing request bodies (resets on Disconnect). */
	int32 NextRpcRequestId = 1;
	TSharedRef<struct FMCPHttpClientState, ESPMode::ThreadSafe> State;

	void SendRequestAsync(
		const FString& Method,
		const TSharedPtr<FJsonObject>& Params,
		bool bExpectResponse,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete);

	/** Parse the JSON-RPC result out of an SSE response body. */
	static bool ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult);

	/** Extract content[0].text from a tools/call result object. */
	static bool ExtractToolResult(const TSharedPtr<FJsonObject>& Result, FString& OutText);
};
