// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UPanelSlot;
class UPanelWidget;
class UTexture2D;
class FDesign2UMGComponentMappingBuildState;
struct FDesign2UMGGenerationSettingsSnapshot;

/**
 * Builds live UWidget trees from generated UMG IR nodes: widget construction,
 * class resolution, property/slot application, and generated-name handling.
 */
class FUMGWidgetTreeBuilder
{
public:
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr, const FIntPoint* ExplicitDesignSize = nullptr, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static UWidget* BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);

	static UClass* ResolveWidgetClass(const FString& TypeName);
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

	/**
	 * Parse generated widget names for reimport matching.
	 * Ranks: base -> 0, base_<N> -> 2N-1, base_w<N> -> 2N; non-matches -> MAX_int32.
	 * Keep these predicates aligned with BuildWidgetTree name generation.
	 */
	static bool MatchesWidgetBaseName(const FString& WidgetName, const FString& BaseName);
	static int32 GetWidgetNameSuffixRank(const FString& WidgetName, const FString& BaseName);

	/**
	 * Resolves a widget in the built tree by its sanitized base name, tolerating the tree builder's
	 * numeric dedup tail ("name" vs "name_2") but nothing else. Returns the widget only when exactly
	 * one tree widget of the requested class matches. Shared by the navigation graph wiring and the
	 * transition animation builder so both resolve the same final tree widget.
	 */
	template <typename WidgetType>
	static WidgetType* FindUniqueTreeWidget(const UWidgetTree* WidgetTree, const FString& BaseName)
	{
		if (!WidgetTree || BaseName.IsEmpty())
		{
			return nullptr;
		}

		TArray<WidgetType*> Matches;
		WidgetTree->ForEachWidget([&BaseName, &Matches](UWidget* Widget)
		{
			WidgetType* Candidate = Cast<WidgetType>(Widget);
			if (Candidate && MatchesWidgetBaseName(Widget->GetName(), BaseName))
			{
				Matches.Add(Candidate);
			}
		});
		return Matches.Num() == 1 ? Matches[0] : nullptr;
	}
};
