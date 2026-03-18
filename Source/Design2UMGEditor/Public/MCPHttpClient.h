// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * MCP Streamable HTTP 瀹㈡埛绔? * 瀹炵幇 JSON-RPC 2.0 + Mcp-Session-Id 鍗忚
 * 鍝嶅簲鏍煎紡涓?SSE (text/event-stream)
 */
class FMCPHttpClient
{
public:
	/**
	 * 杩炴帴鍒?MCP Server 骞跺畬鎴愭彙鎵嬨€?	 * 鍐呴儴鎵ц锛?	 *   1. POST initialize (Accept: text/event-stream) 鈫?浠庡搷搴斿ご鎻愬彇 Mcp-Session-Id
	 *   2. POST notifications/initialized (鎼哄甫 Mcp-Session-Id)
	 * 鎴愬姛鍚?IsConnected() 杩斿洖 true銆?	 */
	bool Connect(const FString& Endpoint);
	void ConnectAsync(const FString& Endpoint, TFunction<void(bool bSuccess)> OnComplete);

	/** 鏂紑杩炴帴 */
	void Disconnect();

	/**
	 * 璋冪敤 MCP 宸ュ叿锛堝悓姝ワ紝闃诲 Game Thread锛夈€?	 * OutResult 涓?result.content[0].text 瑙ｅ寘鍚庣殑绾?JSON 瀛楃涓层€?	 *
	 * 鍝嶅簲鏍煎紡涓?SSE锛屽鎴风鎻愬彇 "data:" 琛屽悗鐨?JSON-RPC body銆?	 * 涓嶆敮鎸佸苟鍙戣皟鐢紝蹇呴』绛変笂涓€娆″畬鎴愬悗鍐嶅彂璧蜂笅涓€娆°€?	 */
	bool CallTool(const FString& ToolName,
				  const TSharedPtr<FJsonObject>& Arguments,
				  FString& OutResult);

	/**
	 * 璋冪敤 MCP 宸ュ叿锛堝紓姝ワ紝涓嶉樆濉?Game Thread锛夈€?	 * 鍥炶皟鍦?Game Thread 瑙﹀彂銆俁esult 鏍煎紡鍚?CallTool銆?	 * 涓嶆敮鎸佸苟鍙戣皟鐢紝蹇呴』绛変笂涓€娆″畬鎴愬悗鍐嶅彂璧蜂笅涓€娆°€?	 */
	void CallToolAsync(const FString& ToolName,
					   const TSharedPtr<FJsonObject>& Arguments,
					   TFunction<void(bool bSuccess, const FString& Result)> OnComplete);

	bool IsConnected() const { return !SessionId.IsEmpty(); }

private:
	FString ServerEndpoint;
	FString SessionId;
	int32 NextRequestId = 1;

	/** 鍙戦€?JSON-RPC 璇锋眰骞剁瓑寰?SSE 鍝嶅簲 */
	bool SendRequest(const FString& Method,
					 const TSharedPtr<FJsonObject>& Params,
					 FString& OutResponseBody,
					 bool bExpectResponse = true);
	void SendRequestAsync(
		const FString& Method,
		const TSharedPtr<FJsonObject>& Params,
		bool bExpectResponse,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& NewSessionId)> OnComplete);

	/** 浠?SSE 鍝嶅簲浣撲腑鎻愬彇 JSON-RPC result */
	static bool ParseSSEResponse(const FString& SSEBody, TSharedPtr<FJsonObject>& OutResult);

	/** 浠?tools/call result 涓彁鍙?content[0].text */
	static bool ExtractToolResult(const TSharedPtr<FJsonObject>& Result, FString& OutText);
};

