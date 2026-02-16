// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * MCP Streamable HTTP 客户端
 * 实现 JSON-RPC 2.0 + Mcp-Session-Id 协议
 * 响应格式为 SSE (text/event-stream)
 */
class FMCPHttpClient
{
public:
	/**
	 * 连接到 MCP Server 并完成握手。
	 * 内部执行：
	 *   1. POST initialize (Accept: text/event-stream) → 从响应头提取 Mcp-Session-Id
	 *   2. POST notifications/initialized (携带 Mcp-Session-Id)
	 * 成功后 IsConnected() 返回 true。
	 */
	bool Connect(const FString& Endpoint);

	/** 断开连接 */
	void Disconnect();

	/**
	 * 调用 MCP 工具（同步，阻塞 Game Thread）。
	 * OutResult 为 result.content[0].text 解包后的纯 JSON 字符串。
	 *
	 * 响应格式为 SSE，客户端提取 "data:" 行后的 JSON-RPC body。
	 * 不支持并发调用，必须等上一次完成后再发起下一次。
	 */
	bool CallTool(const FString& ToolName,
				  const TSharedPtr<FJsonObject>& Arguments,
				  FString& OutResult);

	/**
	 * 调用 MCP 工具（异步，不阻塞 Game Thread）。
	 * 回调在 Game Thread 触发。Result 格式同 CallTool。
	 * 不支持并发调用，必须等上一次完成后再发起下一次。
	 */
	void CallToolAsync(const FString& ToolName,
					   const TSharedPtr<FJsonObject>& Arguments,
					   TFunction<void(bool bSuccess, const FString& Result)> OnComplete);

	bool IsConnected() const { return !SessionId.IsEmpty(); }

private:
	FString ServerEndpoint;
	FString SessionId;
	int32 NextRequestId = 1;

	/** 发送 JSON-RPC 请求并等待 SSE 响应 */
	bool SendRequest(const FString& Method,
					 const TSharedPtr<FJsonObject>& Params,
					 FString& OutResponseBody,
					 bool bExpectResponse = true);

	/** 从 SSE 响应体中提取 JSON-RPC result */
	static bool ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult);

	/** 从 tools/call result 中提取 content[0].text */
	static bool ExtractToolResult(const TSharedPtr<FJsonObject>& Result, FString& OutText);
};
