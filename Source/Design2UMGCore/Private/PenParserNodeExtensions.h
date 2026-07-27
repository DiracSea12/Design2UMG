// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PenParserNodeExtensions
{
	struct FParseCallbacks
	{
		void (*ParseNodeFillProperties)(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver) = nullptr;
		void (*ParseNodeStrokeProperties)(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver) = nullptr;
		void (*ApplyNodeBoxProperties)(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver) = nullptr;
		void (*ApplyNodeLayoutProperties)(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties) = nullptr;
		void (*ApplyNodeTextStyleProperties)(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver) = nullptr;
		EDesignTextAlign (*ParseTextAlign)(const FString& Value) = nullptr;
		EDesignTextAlignVertical (*ParseTextAlignVertical)(const FString& Value) = nullptr;
		EDesignTextGrowth (*ParseTextGrowth)(const FString& Value) = nullptr;
		TSharedPtr<FDesignNode> (*ParseNode)(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver) = nullptr;
		void (*AppendParsedChildren)(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren) = nullptr;
	};

	void FinalizePenPaddingRuntimeWrapPolicy(const TSharedPtr<FDesignNode>& Node);
	void ParsePenSemanticMetadata(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& Node, const FDesignVariableResolver& Resolver);
	void ParseNodeExtended(
		const TSharedPtr<FJsonObject>& JsonObj,
		TSharedPtr<FDesignNode>& Node,
		const FDesignVariableResolver& Resolver,
		const FParseCallbacks& Callbacks);
}
