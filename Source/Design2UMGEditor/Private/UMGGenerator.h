// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"

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
	FDesign2UMGGenerationManifestPayload Manifest;
	TArray<FDesign2UMGMappingDiagnostic> MappingDiagnostics;
	TArray<FDesign2UMGGeneratedComponent> Components;
	/** Completed build transaction, used only to roll back this batch after a failed save. */
	FGuid ComponentBatchTransactionId;
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
		UWidgetBlueprint* ExistingGeneratedBlueprint = nullptr);

	/**
	 * Compile and save the WBP; when a generation output is passed in, atomically write the manifest sidecar next to the asset after a successful save.
	 * Save failure writes no manifest (never a half-updated asset + new manifest combination);
	 * if the asset saved but the sidecar failed, the import still counts as success; the write failure is appended
	 * to GenerationOutput->MappingDiagnostics as a structured diagnostic (reaching the import report).
	 *
	 * PrototypeNavigation: when provided (even when it carries no bindings), the save re-authors the engine-standard
	 * graph wiring for the blueprint's prototype navigation before compiling - first clearing nodes from a previous
	 * generation, so re-imports and regenerates never accumulate. Null leaves the graph untouched. Pass the
	 * navigation data BuildWidgetBlueprint exported through OutNavigationData so the wiring sees the generated
	 * transition animation names.
	 */
	static bool SaveWidgetBlueprint(
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput* GenerationOutput = nullptr,
		const FUMGPrototypeNavigationData* PrototypeNavigation = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test injection (specific to reimport save-failure semantics): once armed, the next SaveWidgetBlueprint fails
	 * exactly once and touches no state; used to verify deterministically the "transaction rollback / manifest not updated" path. Production is unaffected.
	 */
	static void ArmAutomationSaveFailureOnce();
#endif

	/** Failure cleanup for component batches whose existing assets passed the clean/owned preflight. */
	static bool RestoreUnsavedComponentBatch(const TArray<UWidgetBlueprint*>& Assets, FDesign2UMGGenerationOutput* Output);
private:
	static bool SaveSingleWidgetBlueprint(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* GenerationOutput,
		const FUMGPrototypeNavigationData* PrototypeNavigation);
};

