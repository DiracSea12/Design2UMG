// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignImportTypes.h"

class FJsonObject;
class FJsonValue;

/**
 * Parser for .pen document JSON.
 */
class FPenParser
{
public:
	/** Parse a document from a JSON string. */
	static bool ParseDocument(const FString& JsonString, FDesignDocument& OutDocument);

	/** Parse a document from a file. */
	static bool ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument);

private:
	/** Parse the top-level document object. */
	static bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument);

	/** Parse a single node. */
	static TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);

	/** Parse the node type. */
	static EDesignNodeType ParseNodeType(const FString& TypeStr);

	/** Parse a design size value. */
	static FDesignSize ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver);

	/** Parse fill data. */
	static FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);

	/** Parse stroke data. */
	static FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);

	/** Parse fill properties onto a node. */
	static void ParseNodeFillProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);

	/** Parse stroke properties onto a node. */
	static void ParseNodeStrokeProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);

	/** Parse padding data. */
	static FDesignPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);

	/** Parse variable definitions. */
	static void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables);

	/** Parse layout mode. */
	static EDesignLayout ParseLayout(const FString& LayoutStr);

	/** Parse justify-content. */
	static EDesignJustifyContent ParseJustifyContent(const FString& Str);

	/** Parse align-items. */
	static EDesignAlignItems ParseAlignItems(const FString& Str);

	/** Parse text alignment. */
	static EDesignTextAlign ParseTextAlign(const FString& Str);

	/** Parse vertical text alignment. */
	static EDesignTextAlignVertical ParseTextAlignVertical(const FString& Str);

	/** Parse text growth mode. */
	static EDesignTextGrowth ParseTextGrowth(const FString& Str);

	/** Parse extended node properties such as text, icon, and ellipse data. */
	static void ParseNodeExtended(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver);

	/** Apply shared box properties such as x/y and width/height. */
	static void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver);

	/** Apply shared layout properties and optional text layout overrides. */
	static void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties);
};
