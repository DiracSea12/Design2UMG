// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "MCPHttpClient.h"

/**
 * Pencil 瀹炰緥鍙戠幇涓?MCP Server 鐢熷懡鍛ㄦ湡绠＄悊
 */
class FPencilConnector
{
public:
	~FPencilConnector();

	/** 鍙戠幇鏈湴 MCP Server 浜岃繘鍒惰矾寰?*/
	static FString DiscoverMCPServerPath();

	/** 检测 Pencil 应用名称（vscode/antigravity）。 */
	static FString DetectAppName();

	/** 鍚姩 MCP Server 骞惰繛鎺ワ紝Port=0 鏃惰嚜鍔ㄩ€夋嫨 */
	bool Launch(int32 Port = 0);
	void LaunchAsync(int32 Port, TFunction<void(bool bSuccess)> OnComplete);

	/** 鍏抽棴 MCP Server 瀛愯繘绋?*/
	void Shutdown();

	/** 鑾峰彇 MCP HTTP 瀹㈡埛绔?*/
	FMCPHttpClient& GetClient() { return Client; }

	bool IsRunning() const;

private:
	mutable FProcHandle ServerProcess;
	FMCPHttpClient Client;
	int32 ServerPort = 0;
	int32 ActiveLaunchToken = 0;

	/** 鎺㈡祴鍙敤绔彛 */
	static int32 FindAvailablePort(int32 StartPort = 18900);
	void TryConnectAsync(
		const FString& Endpoint,
		double DeadlineSeconds,
		int32 LaunchToken,
		const TSharedRef<TFunction<void(bool bSuccess)>>& OnComplete);
};

