// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGCommitResult.h"
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
	FDesign2UMGCommitResult Commit;
	/** Non-fatal generation loss (for example an unsupported/skipped Prototype binding). */
	bool bHasDegradation = false;
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
	 * Compile/save the WBP and then its manifest, returning the actual commit state.
	 * A single-asset metadata failure leaves the saved asset and its memory intact.
	 * Component batches retain their existing all-or-recover policy.
	 * When supplied, GenerationOutput also receives the result for import callers.
	 *
	 * PrototypeNavigation: when provided (even when it carries no bindings), the save re-authors the engine-standard
	 * graph wiring for the blueprint's prototype navigation before compiling - first clearing nodes from a previous
	 * generation, so re-imports and regenerates never accumulate. Null leaves the graph untouched. Pass the
	 * navigation data BuildWidgetBlueprint exported through OutNavigationData so the wiring sees the generated
	 * transition animation names.
	 */
	static FDesign2UMGCommitResult SaveWidgetBlueprint(
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput* GenerationOutput = nullptr,
		const FUMGPrototypeNavigationData* PrototypeNavigation = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test injection (specific to reimport save-failure semantics): once armed, the next SaveWidgetBlueprint fails
	 * exactly once and touches no state; used to verify deterministically the "transaction rollback / manifest not updated" path. Production is unaffected.
	 */
	static void ArmAutomationSaveFailureOnce();
	/** Skip one original-file restore to exercise incomplete batch recovery. */
	static void ArmAutomationBatchRestoreFailureOnce();
#endif

	/** Failure cleanup for component batches whose existing assets passed the clean/owned preflight. */
	static EDesign2UMGMemoryState RestoreUnsavedComponentBatch(const TArray<UWidgetBlueprint*>& Assets, FDesign2UMGGenerationOutput* Output);
private:
	static FDesign2UMGCommitResult SaveComponentBlueprintBatch(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* Output,
		const FUMGPrototypeNavigationData* Navigation);
	static FDesign2UMGCommitResult SaveSingleWidgetBlueprint(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* GenerationOutput,
		const FUMGPrototypeNavigationData* PrototypeNavigation);
};

