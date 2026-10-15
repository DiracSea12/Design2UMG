// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "MCPStdioClient.h"

/** Discovers the installed Pencil server and owns its connection. */
class DESIGN2UMGEDITOR_API FPencilConnector
{
public:
	~FPencilConnector();
	static FString DiscoverMCPServerPath();
	static FString DetectAppName();
	void LaunchAsync(TFunction<void(bool)> OnComplete);
	void Shutdown();
	FMCPStdioClient& GetClient() { return Client; }
	bool IsRunning() const { return Client.IsRunning(); }

private:
	FMCPStdioClient Client;
};
