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
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords = nullptr, const FIntPoint* ExplicitDesignSize = nullptr, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr);
	static UWidget* BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr);

	static UClass* ResolveWidgetClass(const FString& TypeName);
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize, FString* OutError = nullptr);
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

	/**
	 * Shared TextBlock font-state dump for generation (template stages) and compiled-instance
	 * diagnostics. Callers keep their own gating and pass their owner label pair so each
	 * log line stays byte-identical to its historical form.
	 */
	static void DumpTextBlockFontStates(
		ELogVerbosity::Type Verbosity,
		const TCHAR* Stage,
		UWidgetTree* WidgetTree,
		const TCHAR* OwnerKey,
		const FString& OwnerValue);

	/**
	 * Widget name dedup contract (same source as BuildWidgetTree's base_N dedup rule):
	 * generation-side SanitizeWidgetName+Suffix probing produces base / base_<number>; reimport-side
	 * same-name reconciliation parses with these two predicates. Anchored in one class: changing the dedup rule must update this in sync.
	 */
	static bool MatchesWidgetBaseName(const FString& WidgetName, const FString& BaseName);
	static int32 GetWidgetNameSuffixRank(const FString& WidgetName, const FString& BaseName);

};

