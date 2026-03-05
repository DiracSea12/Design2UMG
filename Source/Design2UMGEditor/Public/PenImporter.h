// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

struct FUMGWidgetData;
struct FUMGWidgetNode;

/**
 * .pen 鏂囦欢瀵煎叆鍏ュ彛
 */
class FPenImporter
{
public:
	/** 浠庢枃浠跺鍏?.pen 骞惰浆鎹负 UMG */
	static bool ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData);

	/** 浠?JSON 瀛楃涓插鍏?*/
	static bool ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData);

	/** 浠?.pen 鏂囦欢瀵煎叆鎸囧畾 TargetNodeId 鐨?frame锛堝 frame 閫夋嫨璺緞锛?*/
	static bool ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData);
};

