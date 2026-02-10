// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPHttpClient.h"

/**
 * Pencil 实例发现与 MCP Server 生命周期管理
 */
class FPencilConnector
{
public:
	~FPencilConnector();

	/** 发现本地 MCP Server 二进制路径 */
	static FString DiscoverMCPServerPath();

	/** 检测 Pencil 应用名称 (antigravity/code) */
	static FString DetectAppName();

	/** 启动 MCP Server 并连接，Port=0 时自动选择 */
	bool Launch(int32 Port = 0);

	/** 关闭 MCP Server 子进程 */
	void Shutdown();

	/** 获取 MCP HTTP 客户端 */
	FMCPHttpClient& GetClient() { return Client; }

	bool IsRunning() const;

private:
	FProcHandle ServerProcess;
	FMCPHttpClient Client;
	int32 ServerPort = 0;

	/** 探测可用端口 */
	static int32 FindAvailablePort(int32 StartPort = 18900);
};
