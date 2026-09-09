// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"

class UWidgetBlueprint;
struct FUMGPrototypeNavigationData;

/** Structured output of one generation: the manifest payload (sidecar written after a successful save) + Component Mapping diagnostics. */
struct FDesign2UMGGenerationOutput
{
	FDesign2UMGGenerationManifestPayload Manifest;
	TArray<FDesign2UMGMappingDiagnostic> MappingDiagnostics;
};

class FUMGGenerator
{
public:
	static UWidgetBlueprint* GenerateWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr);
	static UWidgetBlueprint* BuildWidgetBlueprint(
		const FUMGWidgetData& WidgetData,
		const FString& PackagePath,
		const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr,
		FDesign2UMGGenerationOutput* OutGenerationOutput = nullptr);

	/**
	 * Compile and save the WBP; when a generation output is passed in, atomically write the manifest sidecar next to the asset after a successful save.
	 * Save failure writes no manifest (never a half-updated asset + new manifest combination);
	 * if the asset saved but the sidecar failed, the import still counts as success; the write failure is appended
	 * to GenerationOutput->MappingDiagnostics as a structured diagnostic (reaching the import report).
	 *
	 * PrototypeNavigation: when provided (even when it carries no bindings), the save re-authors the engine-standard
	 * graph wiring for the blueprint's prototype navigation before compiling - first clearing nodes from a previous
	 * generation, so re-imports and regenerates never accumulate. Null leaves the graph untouched.
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
};

