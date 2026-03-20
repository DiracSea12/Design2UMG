// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Margin.h"
#include "UMGSchema.h"

class UWidget;
class UPanelSlot;
class UPanelWidget;
class UWidgetTree;
class UWidgetBlueprint;
class UTexture2D;

/**
 * Shared utilities for creating widgets, applying properties, and rendering previews.
 */
class FUMGWidgetUtils
{
public:
	// ========== Widget creation ==========

	/** Recursively create a widget tree and optionally bind textures for image widgets. */
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);

	/**
	 * Apply layout fixer corrections before widget creation.
	 * @param RootNode Root widget node
	 * @param DesignSize Target UMG design size
	 */
	static void ApplyFixer(FUMGWidgetNode& RootNode, const FIntPoint& DesignSize);

	/** Effective design size used by the fixer and layout calculator. */
	static FIntPoint EffectiveDesignSize;

	/** Calculate the required design size by scanning the full widget tree bounds. */
	static FVector2D CalculateDesignSize(const FUMGWidgetNode& RootNode, FVector2D DefaultSize = FVector2D(1920, 1080));

	// ========== Widget class resolution ==========

	/** Resolve a widget class from a type name. */
	static UClass* ResolveWidgetClass(const FString& TypeName);

	// ========== Color parsing ==========

	/** Parse a color string such as #RRGGBB or a named color. */
	static FLinearColor ParseColor(const FString& ColorStr);

	// ========== Alignment parsing ==========

	static EHorizontalAlignment ParseHAlign(const FString& Str);
	static EVerticalAlignment ParseVAlign(const FString& Str);
	static ESlateVisibility ParseVisibility(const FString& Str);

	// ========== Geometry parsing ==========

	static FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	static FMargin ParseMargin(const FString& Str);

	/** Compare two margins with nearly-equal semantics. */
	static bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left) &&
			FMath::IsNearlyEqual(A.Top, B.Top) &&
			FMath::IsNearlyEqual(A.Right, B.Right) &&
			FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	// ========== Slot property application ==========

	/** Apply slot properties to a panel slot. */
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);

	// ========== Widget property application ==========

	/** Apply legacy string-map widget properties. */
	static void ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties);

	/** Apply typed widget properties with legacy fallback. */
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);

	// ========== Widget rendering ==========

	/** Render a compiled widget blueprint to PNG bytes. */
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	/** Render directly from the widget tree before final save. */
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	// ========== Node id metadata ==========

	/** Widget to source-node-id mapping used by incremental update flows. */
	static TMap<TWeakObjectPtr<UWidget>, FString> NodeIdMap;

	/** Find the source node id for a widget, or return empty when missing. */
	static FString FindNodeId(const UWidget* Widget);

	/** Clear the widget-to-node-id map before each import. */
	static void ClearNodeIdMap() { NodeIdMap.Empty(); }

	// ========== Widget naming ==========

	/** Sanitize a widget name by removing invalid characters. */
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

private:
	/** Apply common widget properties shared across widget types. */
	static void ApplyCommonProperties(UWidget* Widget, const TMap<FString, FString>& Properties);
};
