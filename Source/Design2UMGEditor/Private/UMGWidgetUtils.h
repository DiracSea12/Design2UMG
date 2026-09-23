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
class UWidgetBlueprint;
class FDesign2UMGComponentMappingBuildState;
struct FDesign2UMGGenerationSettingsSnapshot;

class FUMGWidgetUtils
{
public:
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr, const FIntPoint* ExplicitDesignSize = nullptr, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);
	static UWidget* BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState = nullptr, TMap<FString, UClass*>* ClassOverrideCache = nullptr, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);

	static UClass* ResolveWidgetClass(const FString& TypeName);
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);

	/**
	 * Single shared reader for a widget blueprint's UUserWidget CDO DesignTimeSize:
	 * returns the raw size when both axes exceed 1.0f, unset otherwise. Callers keep
	 * their own fallbacks/logging and any ceil-to-FIntPoint conversion.
	 */
	static TOptional<FVector2D> GetBlueprintDesignTimeSize(UWidgetBlueprint* WidgetBlueprint);

	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize, FString* OutError = nullptr, const TOptional<FVector2D>& RenderSizeOverride = TOptional<FVector2D>(), const TOptional<FVector2D>& RootExportOffset = TOptional<FVector2D>());
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
	 * Dump TextBlock font state for generated templates and compiled instances.
	 * Callers supply the owner and stage labels and decide when to log.
	 */
	static void DumpTextBlockFontStates(
		ELogVerbosity::Type Verbosity,
		const TCHAR* Stage,
		UWidgetTree* WidgetTree,
		const TCHAR* OwnerKey,
		const FString& OwnerValue);

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

