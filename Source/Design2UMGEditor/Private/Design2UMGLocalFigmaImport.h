// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FUMGWidgetData;

namespace Design2UMGLocalFigmaImport
{
	bool LoadLocalFigmaDocument(
		const FString& JsonFilePath,
		FDesignDocument& OutDocument,
		FString& OutError,
		const FString& TargetNodeId = FString());
	bool BuildLocalFigmaWidgetData(
		const FString& JsonFilePath,
		FUMGWidgetData& OutData,
		FString& OutError,
		const FString& TargetNodeId = FString());
}
