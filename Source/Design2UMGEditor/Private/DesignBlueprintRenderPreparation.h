// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignPreviewContracts.h"

class UWidgetBlueprint;

/**
 * Render-side blueprint preparation: resets transient renderer/font/raster state
 * and reloads blueprint packages from disk so renders see committed assets.
 */
namespace DesignBlueprintRenderPreparation
{
	/** Clear transient renderer, font, raster, and procedural texture state synchronously. */
	void ResetTransientRenderState();

	// Prepares and reloads the blueprint package for rendering. Explicit options
	// keep callers' font/resource lifetime policy out of global state. A dirty
	// blueprint package is rendered in memory instead of discarding unsaved edits;
	// explicit resource preparation still runs and reports failures.
	UWidgetBlueprint* ReloadWidgetBlueprintForRender(
		UWidgetBlueprint* WidgetBlueprint,
		const FBlueprintRenderPreparationOptions* Preparation = nullptr,
		FString* OutPreparationError = nullptr);

	/** Prepare a blueprint by asset path and optionally reload its package before rendering. */
	UWidgetBlueprint* PrepareWidgetBlueprintForRender(
		const FString& WidgetBlueprintAssetPath,
		const FBlueprintRenderPreparationOptions& Preparation,
		bool bReloadPackage,
		FString* OutPreparationError = nullptr);
}
