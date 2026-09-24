// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "DesignPreviewContracts.h"
#include "UMGGenerator.h"
#include "DesignImportTypes.h"
#include "UObject/StrongObjectPtr.h"

struct FUMGWidgetData;
class UUserWidget;
class UWidgetBlueprint;
class FFeedbackContext;
class SWidget;

namespace DesignImportUtils
{
	void ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data, bool bPersistChanges = true);

	/**
	 * True when the string starts with an http:// or https:// scheme prefix
	 * (case-insensitive). Single shared predicate for the importer, pipeline,
	 * and diagnostic-report URL checks so the scheme set cannot drift between them.
	 */
	bool IsHttpUrl(const FString& Value);

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
	/** Assemble an unsaved import, including source component dependencies when present. */
	UWidgetBlueprint* BuildWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr,
		FUMGPrototypeNavigationData* OutNavigationData = nullptr);

	/** Build and save an import without interactive progress or notifications. */
	UWidgetBlueprint* GenerateWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr);

	UWidgetBlueprint* GenerateBlueprint(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr,
		FFeedbackContext* ProgressFeedback = nullptr,
		bool bPersistIconFontMappingChanges = true);
	bool GenerateBlueprintAndPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		FString* OutGeneratedAssetPath = nullptr,
		FString* OutRenderPngPath = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr,
		bool bPersistIconFontMappingChanges = true);
	bool GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& OutputPngFilePath,
		FString* OutGeneratedAssetPath = nullptr,
		FIntPoint* OutRenderedSize = nullptr,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr,
		FString* OutError = nullptr,
		const FBlueprintRenderPreparationOptions* RenderPreparation = nullptr,
		bool bPersistIconFontMappingChanges = true,
		TStrongObjectPtr<UUserWidget>* OutRenderedWidget = nullptr,
		TSharedPtr<SWidget>* OutRenderedSlateWidget = nullptr);
	bool RenderCompiledBlueprintToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& OutputPngFilePath,
		FIntPoint* OutRenderedSize = nullptr,
		FString* OutError = nullptr,
		const TOptional<FVector2D>& RenderSizeOverride = TOptional<FVector2D>(),
		const TOptional<FVector2D>& RootExportOffset = TOptional<FVector2D>(),
		TStrongObjectPtr<UUserWidget>* OutRenderedWidget = nullptr,
		TSharedPtr<SWidget>* OutRenderedSlateWidget = nullptr);
}

