// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignComponentMapping.h"
#include "Design2UMGComponentMappingConfig.generated.h"

/** Component Mapping diagnostic entry: kind classification + human-readable message, destined for the import diagnostic report. */
struct FDesign2UMGMappingDiagnostic
{
	FString Kind;
	FString Message;
};

/** Raw line structure of a single input declaration in the ini, parsed by the UE config system. */
USTRUCT()
struct FDesignComponentMappingInputLine
{
	GENERATED_BODY()

	UPROPERTY()
	FString Source;

	UPROPERTY()
	FString Target;

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString ValueKind;
};

/** Raw line structure of a single mapping declaration in the ini, parsed by the UE config system. */
USTRUCT()
struct FDesignComponentMappingEntryLine
{
	GENERATED_BODY()

	UPROPERTY()
	FString Key;

	UPROPERTY()
	FString WidgetClass;

	UPROPERTY()
	TArray<FDesignComponentMappingInputLine> Inputs;
};

/**
 * Load result of the project-level Component Mapping config: validated match table + validation diagnostics.
 * Passed into the generation chain as a plain value scoped to the import request lifetime (inside the generation settings snapshot); never stored in globals.
 */
struct FDesign2UMGComponentMappingConfig
{
	DesignComponentMapping::FComponentMappingTable Table;
	TArray<FDesign2UMGMappingDiagnostic> ValidationDiagnostics;

	bool HasMappings() const { return !Table.IsEmpty(); }
};

namespace Design2UMGComponentMappingConfig
{
	/**
	 * Read and validate the [/Design2UMG/ComponentMapping] section of Config/Design2UMGComponentMapping.ini.
	 * A missing ini counts as unconfigured (the normal case; silently returns an empty table); when present, full validation:
	 * three-segment Key format, Provider must be figma (Pencil is not in the v1 main path), WidgetClass resolvable
	 * and a UUserWidget subclass, Inputs declarations inside the closed set. Illegal entries/inputs are dropped with diagnostics.
	 * IniPath exists only for tests to inject a temporary ini; production uses the project Config directory.
	 */
	FDesign2UMGComponentMappingConfig Load(const FString& IniPathOverride = FString());
}
