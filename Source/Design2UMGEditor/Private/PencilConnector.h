// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "MCPHttpClient.h"

/**
 * Discovers, launches, and manages the Pencil MCP server lifecycle.
 */
class DESIGN2UMGEDITOR_API FPencilConnector
{
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FPencilConnectorTestAccess;
#endif

public:
	~FPencilConnector();

	/** Discover the local MCP server executable path. */
	static FString DiscoverMCPServerPath();

	/** Detect the Pencil application name from settings, ~/.pencil/apps, and running IDE processes. */
	static FString DetectAppName();

	/** Launch the MCP server and connect to it asynchronously. Port 0 chooses automatically. */
	void LaunchAsync(int32 Port, TFunction<void(bool bSuccess)> OnComplete);

	/** Shut down the MCP server child process. */
	void Shutdown();

	/** Access the MCP HTTP client. */
	FMCPHttpClient& GetClient() { return *Client; }

	bool IsRunning() const;

private:
	/**
	 * Shared per-launch state. The connect/retry callbacks capture a shared
	 * pointer to this (plus value copies of what they need) instead of
	 * capturing the connector, so a callback that outlives the connector
	 * never touches freed memory. The cancelled flag is the generation
	 * check: Shutdown and every new LaunchAsync mark the previous attempt
	 * cancelled, and each callback re-validates it before running.
	 */
	struct FLaunchSession
	{
		explicit FLaunchSession(const TSharedRef<FMCPHttpClient>& InClient)
			: Client(InClient)
		{
		}

		/** Set when this attempt is superseded or shut down. */
		bool bCancelled = false;
		FProcHandle ServerProcess;
		/** Shared alias of the connector's retry ticker handle (single source of truth for the pending retry). */
		TSharedPtr<FTSTicker::FDelegateHandle> TickerHandle;
		/** The same client object GetClient() exposes, shared so callbacks can drive the handshake. */
		TSharedRef<FMCPHttpClient> Client;
	};

	TSharedPtr<FLaunchSession, ESPMode::ThreadSafe> ActiveLaunchSession;
	/** Retry ticker handle shared with the active launch session; recreated by each LaunchAsync. */
	TSharedPtr<FTSTicker::FDelegateHandle> RetryTickerHandle = MakeShared<FTSTicker::FDelegateHandle>();
	/** Client-lifetime MCP HTTP client; shared with the active launch session. */
	TSharedRef<FMCPHttpClient> Client = MakeShared<FMCPHttpClient>();

	/** Find an available local port. */
	static int32 FindAvailablePort(int32 StartPort = 18900);
	void ScheduleRetry(FTickerDelegate&& RetryDelegate, float DelaySeconds);
	static void TryConnectAsync(
		const TSharedPtr<FLaunchSession, ESPMode::ThreadSafe>& Session,
		const FString& Endpoint,
		double DeadlineSeconds,
		const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete);
	/** Cancel and tear down one launch attempt through shared state only (no connector access). */
	static void ShutdownLaunchSession(const TSharedPtr<FLaunchSession, ESPMode::ThreadSafe>& Session);
};
