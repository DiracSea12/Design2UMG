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

	static bool BuildFilteredDocumentObject(
		const TSharedPtr<FJsonObject>& DocObj,
		TSharedPtr<FJsonObject>& OutDocumentObject,
		const FString& TargetNodeId = TEXT(""));

	// The user-selected target node is the render root of this import, not a
	// reusable component definition: clear its reusable flag after parsing so the
	// converter does not skip the whole page. Other reusable definitions stay in
	// the document for ref expansion.
	static void PromoteSelectedNodeToRenderRoot(FDesignDocument& Document, const FString& TargetNodeId);
};

