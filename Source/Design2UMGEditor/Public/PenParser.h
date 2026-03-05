// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignImportTypes.h"

class FJsonObject;
class FJsonValue;

/**
 * .pen 鏂囦欢 JSON 瑙ｆ瀽鍣? */
class FPenParser
{
public:
	/** 浠?JSON 瀛楃涓茶В鏋愭枃妗?*/
	static bool ParseDocument(const FString& JsonString, FDesignDocument& OutDocument);

	/** 浠庢枃浠惰В鏋愭枃妗?*/
	static bool ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument);

private:
	/** 瑙ｆ瀽椤跺眰鏂囨。 */
	static bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument);

	/** 瑙ｆ瀽鑺傜偣 */
	static TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver, float ParentX = 0.0f, float ParentY = 0.0f);

	/** 瑙ｆ瀽鑺傜偣绫诲瀷 */
	static EDesignNodeType ParseNodeType(const FString& TypeStr);

	/** 瑙ｆ瀽灏哄鍊?*/
	static FDesignSize ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver);

	/** 瑙ｆ瀽濉厖 */
	static FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);

	/** 瑙ｆ瀽鎻忚竟 */
	static FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);

	/** 瑙ｆ瀽鍐呰竟璺?*/
	static FDesignPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);

	/** 瑙ｆ瀽鍙橀噺瀹氫箟 */
	static void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables);

	/** 瑙ｆ瀽甯冨眬妯″紡 */
	static EDesignLayout ParseLayout(const FString& LayoutStr);

	/** 瑙ｆ瀽涓昏酱瀵归綈 */
	static EDesignJustifyContent ParseJustifyContent(const FString& Str);

	/** 瑙ｆ瀽浜ゅ弶杞村榻?*/
	static EDesignAlignItems ParseAlignItems(const FString& Str);

	/** 瑙ｆ瀽鏂囨湰瀵归綈 */
	static EDesignTextAlign ParseTextAlign(const FString& Str);

	/** 瑙ｆ瀽鏂囨湰鍨傜洿瀵归綈 */
	static EDesignTextAlignVertical ParseTextAlignVertical(const FString& Str);

	/** 瑙ｆ瀽鏂囨湰澧為暱妯″紡 */
	static EDesignTextGrowth ParseTextGrowth(const FString& Str);

	/** 瑙ｆ瀽鑺傜偣鎵╁睍灞炴€э紙鏂囨湰銆佸浘鏍囥€佹き鍦嗙瓑锛?*/
	static void ParseNodeExtended(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver);
};




