// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "DesignPreviewContracts.h"
#include "UMGGenerator.h"

#include "UObject/StrongObjectPtr.h"

struct FUMGWidgetData;
class UUserWidget;
class UWidgetBlueprint;
class FFeedbackContext;
class SWidget;

namespace DesignImportGeneration
{
	void ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data, bool bPersistChanges = true);

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
		FIntPoint* OutRenderedSize,
		FString* OutError,
		const TOptional<FVector2D>& RenderSizeOverride = TOptional<FVector2D>(),
		const TOptional<FVector2D>& RootExportOffset = TOptional<FVector2D>(),
		TStrongObjectPtr<UUserWidget>* OutRenderedWidget = nullptr,
		TSharedPtr<SWidget>* OutRenderedSlateWidget = nullptr);
}
