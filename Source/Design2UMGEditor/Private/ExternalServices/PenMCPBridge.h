// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FDesign2UMGImportSettingsSnapshot;
struct FDesignDocument;
struct FUMGWidgetData;

class FPenMCPBridge
{
public:
	/** Parse the live snapshot; SourceFilePath only anchors relative resources. */
	static bool ParseFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FDesignDocument& OutDocument,
		const FString& TargetNodeId = TEXT(""),
		const FString& SourceFilePath = TEXT(""));

	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT(""));
	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT(""));

	// Prepare the source with its definitions intact; after parsing, call
	// PromoteSelectedNodeToRenderRoot to restrict the output roots.
	static bool BuildFilteredDocumentObject(
		const TSharedPtr<FJsonObject>& DocObj,
		TSharedPtr<FJsonObject>& OutDocumentObject,
		const FString& TargetNodeId = TEXT(""));

	// Keep the selected output root, library/reusable definitions and referenced
	// ordinary targets. Definitions retain their authored reusable flags.
	static void PromoteSelectedNodeToRenderRoot(FDesignDocument& Document, const FString& TargetNodeId);
};

