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
	 * Read Config/Design2UMGComponentMapping.ini; a missing file yields an empty table.
	 * Validate Figma keys, UUserWidget classes and supported input declarations.
	 * Invalid entries are dropped with diagnostics. IniPath overrides the project config path.
	 */
	FDesign2UMGComponentMappingConfig Load(const FString& IniPathOverride = FString());
}
