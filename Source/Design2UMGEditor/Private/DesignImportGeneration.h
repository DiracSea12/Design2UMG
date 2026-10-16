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

struct FArtifactGenerationResult
{
	FDesign2UMGCommitResult Commit;
	/** Warnings from the actual generation attempt, including resource resolution. */
	TArray<FString> Warnings;
	/** True only when commit and requested artifacts succeeded; render can fail after commit. */
	bool bSucceeded = false;
	/** Structured failure reason (e.g. render target size above the RHI texture limit); empty on success or unspecified failures. */
	FString ErrorMessage;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
	/** Actual compiled widget instance used for the render, retained for immediate caller-side inspection. */
	TStrongObjectPtr<UUserWidget> RenderedWidget;
	/** Keeps that render's Slate tree alive so its cached geometry remains inspectable. */
	TSharedPtr<SWidget> RenderedSlateWidget;

	/** Component Mapping diagnostics (ini validation + hit/fallback/inputs/Prototype boundaries), going into the import diagnostic report. */
	TArray<FDesign2UMGMappingDiagnostic> ComponentMappingDiagnostics;
};

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
	FArtifactGenerationResult GenerateBlueprintAndPng(
		const FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
	FArtifactGenerationResult GenerateBlueprintAndCompiledPng(
		const FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr);
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
