// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImageResourceLimits.h"
#include "UObject/StrongObjectPtr.h"

class UUserWidget;
class UWidgetBlueprint;
class UWidgetTree;
class SWidget;

struct FUMGWidgetExportPlan
{
	FIntPoint Size = FIntPoint::ZeroValue;
	FIntPoint TileSize = FIntPoint::ZeroValue;
	FIntPoint TileCount = FIntPoint::ZeroValue;
};

/**
 * Renders widget blueprints and trees to PNG data, plus the render-adjacent
 * readers and pixel helpers those renders rely on.
 */
class FUMGWidgetRenderer
{
public:
	// Screenshot output and hardware-compatible targets; input-image resource limits are separate.
	static constexpr int64 MaxExportPixels = DesignImageResources::RenderExportMaxPixels;
	static bool BuildExportPlan(const FIntPoint& Size, int32 MaxTextureDimension, FUMGWidgetExportPlan& OutPlan, FString* OutError = nullptr);

	/**
	 * Single shared reader for a widget blueprint's UUserWidget CDO DesignTimeSize:
	 * returns the raw size when both axes are positive, unset otherwise. Callers keep
	 * their own fallbacks/logging and any ceil-to-FIntPoint conversion.
	 */
	static TOptional<FVector2D> GetBlueprintDesignTimeSize(UWidgetBlueprint* WidgetBlueprint);

	static void SetBlueprintExportBounds(UWidgetBlueprint* WidgetBlueprint, const TOptional<FBox2D>& Bounds);
	static FBox2D GetBlueprintExportBounds(UWidgetBlueprint* WidgetBlueprint);

	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize, FString* OutError = nullptr, const TOptional<FVector2D>& RenderSizeOverride = TOptional<FVector2D>(), const TOptional<FVector2D>& RootExportOffset = TOptional<FVector2D>(), TStrongObjectPtr<UUserWidget>* OutRenderedWidget = nullptr, TSharedPtr<SWidget>* OutRenderedSlateWidget = nullptr);
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	/**
	 * Slate render targets store premultiplied alpha (RGB scaled against a
	 * transparent black base), while Pencil/Figma references use straight alpha.
	 * Converts read-back pixels in place to straight alpha before PNG save:
	 * A=255 pixels are untouched, A=0 pixels get RGB zeroed, partial alpha is
	 * un-premultiplied as min(255, round(RGB * 255 / A)).
	 */
	static void UnpremultiplyPixels(TArray<FColor>& Pixels);

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
};
