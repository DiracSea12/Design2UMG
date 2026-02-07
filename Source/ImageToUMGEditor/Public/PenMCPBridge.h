// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;

/**
 * Pencil MCP → PenDocument JSON 桥接层
 *
 * 接收 Pencil MCP 的 batch_get / get_variables 返回数据，
 * 组装为 PenParser::ParseDocument 期望的完整文档 JSON。
 */
class FPenMCPBridge
{
public:
	/**
	 * 从 MCP 返回数据组装 PenDocument JSON
	 *
	 * @param NodesJson      batch_get() 返回的 JSON 数组字符串（嵌套树结构）
	 * @param VariablesJson  get_variables() 返回的 JSON 对象字符串（含 variables/themes）
	 * @param TargetNodeId   可选，只导出指定 frame 节点
	 * @return 符合 PenParser 期望的完整文档 JSON，失败返回空字符串
	 */
	static FString AssembleDocumentJson(
		const FString& NodesJson,
		const FString& VariablesJson,
		const FString& TargetNodeId = TEXT("")
	);

	/**
	 * 一步到位：组装 + 解析 + 转换
	 *
	 * @param NodesJson      batch_get() 返回的 JSON 数组
	 * @param VariablesJson  get_variables() 返回的 JSON 对象
	 * @param OutData        输出的 UMG Widget 数据
	 * @param TargetNodeId   可选，只导出指定 frame 节点
	 * @return 成功返回 true
	 */
	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT("")
	);
};
