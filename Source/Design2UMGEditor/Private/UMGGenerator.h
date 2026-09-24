// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGCommitResult.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"

namespace DesignImageResources { class FImageOperationBudget; }

class UWidgetBlueprint;
struct FUMGPrototypeNavigationData;
struct FDesign2UMGGenerationOutput;

struct FDesign2UMGGeneratedComponent
{
	UWidgetBlueprint* Blueprint = nullptr;
	TSharedPtr<FDesign2UMGGenerationOutput> Output;
};

/** Structured output of one generation: the manifest payload (sidecar written after a successful save) + Component Mapping diagnostics. */
struct FDesign2UMGGenerationOutput
{
	FDesign2UMGCommitResult Commit;
	/** Non-fatal generation loss (for example an unsupported/skipped Prototype binding). */
	bool bHasDegradation = false;
	FDesign2UMGGenerationManifestPayload Manifest;
	TArray<FDesign2UMGMappingDiagnostic> MappingDiagnostics;
	TArray<FDesign2UMGGeneratedComponent> Components;
	/** Completed build transaction, used only to roll back this batch after a failed save. */
	FGuid ComponentBulkTransactionId;
};

class FUMGGenerator
{
public:
	static UWidgetBlueprint* GenerateWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr);
	/**
	 * Assembles the unsaved widget blueprint (widget tree + generated transition animations).
	 * OutNavigationData: receives the navigation IR copy enriched with the generated transition
	 * animation names; hand that same data to SaveWidgetBlueprint so the graph wiring plays them.
	 */
	static UWidgetBlueprint* BuildWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr,
		FUMGPrototypeNavigationData* OutNavigationData = nullptr,
		UWidgetBlueprint* ExistingGeneratedBlueprint = nullptr, DesignImageResources::FImageOperationBudget* Budget = nullptr);

	/**
	 * Compile/save the WBP, then its manifest, and return the actual commit state.
	 * Metadata failure preserves the saved asset; component batches use all-or-recover.
	 * GenerationOutput receives the result when supplied.
	 * Non-null PrototypeNavigation replaces generated graph wiring before compilation;
	 * null leaves it untouched. Use the navigation output from BuildWidgetBlueprint.
	 */
	static FDesign2UMGCommitResult SaveWidgetBlueprint(
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput* GenerationOutput = nullptr,
		const FUMGPrototypeNavigationData* PrototypeNavigation = nullptr);

	/** Failure cleanup for component batches whose existing assets passed the clean/owned preflight. */
	static EDesign2UMGMemoryState RestoreUnsavedComponentBulk(const TArray<UWidgetBlueprint*>& Assets, FDesign2UMGGenerationOutput* Output);
private:
	static FDesign2UMGCommitResult SaveComponentBlueprintBulk(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* Output,
		const FUMGPrototypeNavigationData* Navigation);
	static FDesign2UMGCommitResult SaveSingleWidgetBlueprint(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* GenerationOutput,
		const FUMGPrototypeNavigationData* PrototypeNavigation);
};
