// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"
#include "PenVariableResolver.h"

/**
 * .pen 文件 JSON 解析器
 */
class FPenParser
{
public:
	/** 从 JSON 字符串解析文档 */
	static bool ParseDocument(const FString& JsonString, FPenDocument& OutDocument);

	/** 从文件解析文档 */
	static bool ParseFromFile(const FString& FilePath, FPenDocument& OutDocument);

private:
	/** 解析顶层文档 */
	static bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FPenDocument& OutDocument);

	/** 解析节点 */
	static TSharedPtr<FPenNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FPenVariableResolver& Resolver);

	/** 解析节点类型 */
	static EPenNodeType ParseNodeType(const FString& TypeStr);

	/** 解析尺寸值 */
	static FPenSize ParseSize(const TSharedPtr<FJsonValue>& Value, EPenNodeType NodeType, const FPenVariableResolver& Resolver);

	/** 解析填充 */
	static FPenFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FPenVariableResolver& Resolver);

	/** 解析描边 */
	static FPenStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FPenVariableResolver& Resolver);

	/** 解析内边距 */
	static FPenPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FPenVariableResolver& Resolver);

	/** 解析变量定义 */
	static void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FPenVariable>& OutVariables);

	/** 解析布局模式 */
	static EPenLayout ParseLayout(const FString& LayoutStr);

	/** 解析主轴对齐 */
	static EPenJustifyContent ParseJustifyContent(const FString& Str);

	/** 解析交叉轴对齐 */
	static EPenAlignItems ParseAlignItems(const FString& Str);

	/** 解析文本对齐 */
	static EPenTextAlign ParseTextAlign(const FString& Str);

	/** 解析文本垂直对齐 */
	static EPenTextAlignVertical ParseTextAlignVertical(const FString& Str);

	/** 解析文本增长模式 */
	static EPenTextGrowth ParseTextGrowth(const FString& Str);

	/** 解析节点扩展属性（文本、图标、椭圆等） */
	static void ParseNodeExtended(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FPenNode>& Node, const FPenVariableResolver& Resolver);
};
