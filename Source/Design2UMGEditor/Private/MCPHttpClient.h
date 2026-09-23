// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"

class FJsonObject;

/**
 * Streamable MCP HTTP client implementing JSON-RPC 2.0 with Mcp-Session-Id.
 * Responses are received through SSE (text/event-stream).
 */
class DESIGN2UMGEDITOR_API FMCPHttpClient
{
private:
	struct FInFlightCancelState;
	struct FConnectSession;

public:
	explicit FMCPHttpClient(FHttpModule& HttpModule = FHttpModule::Get());
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

	bool IsConnected() const { return Session.IsValid() && !Session->SessionId.IsEmpty(); }


private:
	/** Shared delivered-flag for the in-flight request's cancel-failure path. */
	struct FInFlightCancelState
	{
		bool bDelivered = false;
	};

	/**
	 * Shared connection-session state. Async callbacks (handshake hops, HTTP
	 * completions) capture a shared pointer to this instead of the client
	 * object, so a late callback never dereferences a destroyed client.
	 * Replaced wholesale by each ConnectAsync after Disconnect.
	 */
	struct FConnectSession
	{
		FString ServerEndpoint;
		FString SessionId;
		/** JSON-RPC id embedded in outgoing request bodies (fresh per session). */
		int32 NextRpcRequestId = 1;
		/**
		 * Deliver failure once when a request is superseded.
		 * Disconnect clears this callback without delivery.
		 */
		TSharedPtr<FInFlightCancelState, ESPMode::ThreadSafe> InFlightCancelState;
		TFunction<void()> InFlightCancelFailure;
	};

	/** Client-lifetime request slot; its tracking ids stay monotonic across sessions. */
	TSharedRef<class FDesignHttpClientSlot, ESPMode::ThreadSafe> State;
	/** Current connection session; null while disconnected. */
	TSharedPtr<FConnectSession, ESPMode::ThreadSafe> Session;

	/**
	 * Send a JSON-RPC request using shared slot/session state, without capturing the client.
	 * An empty LogLabel uses Method. When bLogTransportErrors is false, the caller owns
	 * connection/HTTP error logging; OnComplete still receives the failure.
	 */
	static void SendRequestAsync(
		const TSharedRef<class FDesignHttpClientSlot, ESPMode::ThreadSafe>& State,
		const TSharedPtr<FConnectSession, ESPMode::ThreadSafe>& Session,
		const FString& Method,
		const TSharedPtr<FJsonObject>& Params,
		bool bExpectResponse,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete,
		const FString& LogLabel = FString(),
		bool bLogTransportErrors = true);

	/**
	 * Marks the current request delivered and, when it is still the session's current
	 * request, drops the session-owned cancel callback. The callback capture often owns
	 * the session itself (ConnectAsync handshake callbacks capture Conn), so clearing it
	 * here is what keeps the session from becoming a self-referencing cycle.
	 */
	static void FinishInFlightRequest(
		const TWeakPtr<FConnectSession, ESPMode::ThreadSafe>& Session,
		const TSharedPtr<FInFlightCancelState, ESPMode::ThreadSafe>& CancelState);

	/** Parse the JSON-RPC result out of an SSE response body. */
	static bool ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult);

	/** Extract content[0].text from a tools/call result object. */
	static bool ExtractToolResult(const TSharedPtr<FJsonObject>& Result, FString& OutText);
};
