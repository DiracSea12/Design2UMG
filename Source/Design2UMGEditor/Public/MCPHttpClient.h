// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Streamable MCP HTTP client implementing JSON-RPC 2.0 with Mcp-Session-Id.
 * Responses are received through SSE (text/event-stream).
 */
class FMCPHttpClient
{
public:
	/**
	 * Connect to the MCP server and complete the handshake.
	 * Internally this sends initialize, extracts Mcp-Session-Id, then sends
	 * notifications/initialized. IsConnected() returns true on success.
	 */
	bool Connect(const FString& Endpoint);
	void ConnectAsync(const FString& Endpoint, TFunction<void(bool bSuccess)> OnComplete);

	/** Disconnect the current session. */
	void Disconnect();

	/**
	 * Call an MCP tool synchronously on the game thread.
	 * OutResult receives the decoded result.content[0].text payload.
	 * Concurrent calls are not supported.
	 */
	bool CallTool(const FString& ToolName,
				  const TSharedPtr<FJsonObject>& Arguments,
				  FString& OutResult);

	/**
	 * Call an MCP tool asynchronously.
	 * The callback is invoked on the game thread with the same payload shape as CallTool.
	 * Concurrent calls are not supported.
	 */
	void CallToolAsync(const FString& ToolName,
					   const TSharedPtr<FJsonObject>& Arguments,
					   TFunction<void(bool bSuccess, const FString& Result)> OnComplete);

	bool IsConnected() const { return !SessionId.IsEmpty(); }

private:
	FString ServerEndpoint;
	FString SessionId;
	int32 NextRequestId = 1;

	/** Send a JSON-RPC request and wait for the SSE response. */
	bool SendRequest(const FString& Method,
					 const TSharedPtr<FJsonObject>& Params,
					 FString& OutResponseBody,
					 bool bExpectResponse = true);
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
