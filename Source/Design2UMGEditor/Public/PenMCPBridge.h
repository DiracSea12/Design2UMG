// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

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

	/**
	 * 从已解析的 JSON 对象直接转换（避免重复序列化/反序列化）
	 *
	 * @param DocObj          已解析的完整文档 JSON 对象
	 * @param OutData         输出的 UMG Widget 数据
	 * @param TargetNodeId    可选，只导出指定 frame 节点
	 * @return 成功返回 true
	 */
	static bool ConvertFromParsedDoc(
		const TSharedPtr<FJsonObject>& DocObj,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT("")
	);

	/**
	 * Build filtered/full pen document JSON from an already parsed document object.
	 *
	 * @param DocObj          Parsed full document JSON object
	 * @param OutDocumentJson Output pen document JSON string
	 * @param TargetNodeId    Optional target frame/node id
	 * @return true if JSON was built successfully
	 */
	static bool BuildFilteredDocumentJson(
		const TSharedPtr<FJsonObject>& DocObj,
		FString& OutDocumentJson,
		const FString& TargetNodeId = TEXT("")
	);
};
