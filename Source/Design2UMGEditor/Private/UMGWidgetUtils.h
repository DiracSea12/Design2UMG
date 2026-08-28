// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "UMGSchema.h"

class UPanelSlot;
class UPanelWidget;
class UTexture2D;
class UWidget;
class UWidgetBlueprint;
class UWidgetTree;
class FDesign2UMGComponentMappingBuildState;

class FUMGWidgetUtils
{
public:
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords = nullptr, const FIntPoint* ExplicitDesignSize = nullptr, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr);
	static UWidget* BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr);

	static UClass* ResolveWidgetClass(const FString& TypeName);
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

	/**
	 * widget 命名去重契约（与 BuildWidgetTree 的 base_N 去重同名规则同源）：
	 * 生成侧 SanitizeWidgetName+Suffix 探测产生 base / base_<数字>；reimport 侧
	 * 同名对账用这两个谓词解析。放在同一个类里锚定：改去重规则必须同步这里。
	 */
	static bool MatchesWidgetBaseName(const FString& WidgetName, const FString& BaseName);
	static int32 GetWidgetNameSuffixRank(const FString& WidgetName, const FString& BaseName);

};

