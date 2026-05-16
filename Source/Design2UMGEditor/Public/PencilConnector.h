// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "MCPHttpClient.h"

/**
 * Discovers, launches, and manages the Pencil MCP server lifecycle.
 */
class FPencilConnector
{
public:
	~FPencilConnector();

	/** Discover the local MCP server executable path. */
	static FString DiscoverMCPServerPath();

	/** Detect the Pencil application name from settings, ~/.pencil/apps, and running IDE processes. */
	static FString DetectAppName();

	/** Launch the MCP server and connect to it. Port 0 chooses automatically. */
	bool Launch(int32 Port = 0);
	void LaunchAsync(int32 Port, TFunction<void(bool bSuccess)> OnComplete);

	/** Shut down the MCP server child process. */
	void Shutdown();

	/** Access the MCP HTTP client. */
	FMCPHttpClient& GetClient() { return Client; }

	bool IsRunning() const;

private:
	mutable FProcHandle ServerProcess;
	FMCPHttpClient Client;
	int32 ServerPort = 0;
	int32 ActiveLaunchToken = 0;

	/** Find an available local port. */
	static int32 FindAvailablePort(int32 StartPort = 18900);
	void TryConnectAsync(
		const FString& Endpoint,
		double DeadlineSeconds,
		int32 LaunchToken,
		const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete);
};
