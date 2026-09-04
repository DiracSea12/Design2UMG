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
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize, FString* OutError = nullptr, const TOptional<FIntPoint>& RenderSizeOverride = TOptional<FIntPoint>());
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	/**
	 * Slate render targets store premultiplied alpha (RGB scaled against a
	 * transparent black base), while Pencil/Figma references use straight alpha.
	 * Converts read-back pixels in place to straight alpha before PNG save:
	 * A=255 pixels are untouched, A=0 pixels get RGB zeroed, partial alpha is
	 * un-premultiplied as min(255, round(RGB * 255 / A)).
	 */
	static void UnpremultiplyPixels(TArray<FColor>& Pixels);
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

