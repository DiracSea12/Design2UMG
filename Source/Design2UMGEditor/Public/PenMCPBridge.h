// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FUMGWidgetData;

/**
 * Pencil MCP 鈫?PenDocument JSON 妗ユ帴灞? *
 * 鎺ユ敹 Pencil MCP 鐨?batch_get / get_variables 杩斿洖鏁版嵁锛? * 缁勮涓?PenParser::ParseDocument 鏈熸湜鐨勫畬鏁存枃妗?JSON銆? */
class FPenMCPBridge
{
public:
	/**
	 * 浠?MCP 杩斿洖鏁版嵁缁勮 PenDocument JSON
	 *
	 * @param NodesJson      batch_get() 杩斿洖鐨?JSON 鏁扮粍瀛楃涓诧紙宓屽鏍戠粨鏋勶級
	 * @param VariablesJson  get_variables() 杩斿洖鐨?JSON 瀵硅薄瀛楃涓诧紙鍚?variables/themes锛?
	 * @param TargetNodeId   鍙€夛紝鍙鍑烘寚瀹?frame 鑺傜偣
	 * @return 绗﹀悎 PenParser 鏈熸湜鐨勫畬鏁存枃妗?JSON锛屽け璐ヨ繑鍥炵┖瀛楃涓?
	 */
	static FString AssembleDocumentJson(
		const FString& NodesJson,
		const FString& VariablesJson,
		const FString& TargetNodeId = TEXT("")
	);

	/**
	 * 涓€姝ュ埌浣嶏細缁勮 + 瑙ｆ瀽 + 杞崲
	 *
	 * @param NodesJson      batch_get() 杩斿洖鐨?JSON 鏁扮粍
	 * @param VariablesJson  get_variables() 杩斿洖鐨?JSON 瀵硅薄
	 * @param OutData        杈撳嚭鐨?UMG Widget 鏁版嵁
	 * @param TargetNodeId   鍙€夛紝鍙鍑烘寚瀹?frame 鑺傜偣
	 * @return 鎴愬姛杩斿洖 true
	 */
	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT("")
	);

	/**
	 * 浠庡凡瑙ｆ瀽鐨?JSON 瀵硅薄鐩存帴杞崲锛堥伩鍏嶉噸澶嶅簭鍒楀寲/鍙嶅簭鍒楀寲锛?
	 *
	 * @param DocObj          宸茶В鏋愮殑瀹屾暣鏂囨。 JSON 瀵硅薄
	 * @param OutData         杈撳嚭鐨?UMG Widget 鏁版嵁
	 * @param TargetNodeId    鍙€夛紝鍙鍑烘寚瀹?frame 鑺傜偣
	 * @return 鎴愬姛杩斿洖 true
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

