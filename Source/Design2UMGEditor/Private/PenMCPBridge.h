// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FDesign2UMGImportSettingsSnapshot;
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
};

