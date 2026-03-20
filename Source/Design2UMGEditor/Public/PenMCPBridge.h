// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FUMGWidgetData;

/**
 * Bridges Pencil MCP payloads into pen document JSON.
 * Accepts batch_get / get_variables responses and assembles the full document
 * format expected by PenParser::ParseDocument.
 */
class FPenMCPBridge
{
public:
	/**
	 * Assemble pen document JSON from MCP payloads.
	 *
	 * @param NodesJson      JSON array returned by batch_get()
	 * @param VariablesJson  JSON object returned by get_variables()
	 * @param TargetNodeId   Optional target frame/node id
	 * @return Full pen document JSON, or an empty string on failure
	 */
	static FString AssembleDocumentJson(
		const FString& NodesJson,
		const FString& VariablesJson,
		const FString& TargetNodeId = TEXT(""));

	/**
	 * Assemble, parse, and convert in one step.
	 *
	 * @param NodesJson      JSON array returned by batch_get()
	 * @param VariablesJson  JSON object returned by get_variables()
	 * @param OutData        Converted widget data
	 * @param TargetNodeId   Optional target frame/node id
	 * @return true on success
	 */
	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT(""));

	/**
	 * Convert directly from an already parsed document object.
	 *
	 * @param DocObj          Parsed full document JSON object
	 * @param OutData         Converted widget data
	 * @param TargetNodeId    Optional target frame/node id
	 * @return true on success
	 */
	static bool ConvertFromParsedDoc(
		const TSharedPtr<FJsonObject>& DocObj,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT(""));

	/**
	 * Build filtered or full pen document JSON from an already parsed document object.
	 *
	 * @param DocObj          Parsed full document JSON object
	 * @param OutDocumentJson Output pen document JSON string
	 * @param TargetNodeId    Optional target frame/node id
	 * @return true if JSON was built successfully
	 */
	static bool BuildFilteredDocumentJson(
		const TSharedPtr<FJsonObject>& DocObj,
		FString& OutDocumentJson,
		const FString& TargetNodeId = TEXT(""));
};
