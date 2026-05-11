// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUMGWidgetData;

class FPenMCPBridge
{
public:
	static bool ConvertFromMCP(
		const FString& NodesJson,
		const FString& VariablesJson,
		FUMGWidgetData& OutData,
		const FString& TargetNodeId = TEXT(""));

	static bool BuildFilteredDocumentObject(
		const TSharedPtr<FJsonObject>& DocObj,
		TSharedPtr<FJsonObject>& OutDocumentObject,
		const FString& TargetNodeId = TEXT(""));
};

