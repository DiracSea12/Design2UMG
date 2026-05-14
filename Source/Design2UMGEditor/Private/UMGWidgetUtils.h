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

class FUMGWidgetUtils
{
public:
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords = nullptr, const FIntPoint* ExplicitDesignSize = nullptr);
	static UWidget* BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize);

	static UClass* ResolveWidgetClass(const FString& TypeName);
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

};

