// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGConverter.h"
#include "DesignPreviewContracts.h"

class UClass;
class FDesign2UMGComponentBlueprintContext;
struct FDesignPreviewMaterializationSettingsSnapshot;

struct FDesign2UMGGenerationSettingsSnapshot
{
	FDesignImageImportSettingsSnapshot ImageSettings;
	TArray<FWidgetClassOverride> ClassOverrides;

	/**
	 * Project-level Component Mapping config (the loaded result of Config/Design2UMGComponentMapping.ini).
	 * Carried into the generation chain with the import settings snapshot, lifetime tied to the snapshot, never a global;
	 * preview materialization (preview narrowing) carries no mapping.
	 */
	FDesign2UMGComponentMappingConfig ComponentMapping;
	FDesign2UMGConversionSettings ComponentConversionSettings = FDesign2UMGConversionSettings::Defaults();
	TSharedPtr<FDesign2UMGComponentBlueprintContext> ComponentBlueprints;

	static FDesign2UMGGenerationSettingsSnapshot FromSettings(const UDesign2UMGSettings& Settings);
	static FDesign2UMGGenerationSettingsSnapshot FromMaterializationSettings(
		const FDesignPreviewMaterializationSettingsSnapshot& MaterializationSettings);

	/**
	 * Resolve the class override for a node name through the shared settings matcher.
	 * The optional cache scopes LoadSynchronous to once per widget-tree build (see
	 * UDesign2UMGSettings::ResolveClassOverride).
	 */
	UClass* FindClassOverride(const FString& NodeName, TMap<FString, UClass*>* InOutNodeNameCache = nullptr) const;
};
