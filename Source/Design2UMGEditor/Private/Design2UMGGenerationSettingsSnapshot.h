// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "Design2UMGComponentMappingConfig.h"

class UClass;
struct FDesignPreviewMaterializationSettingsSnapshot;

struct FDesign2UMGGenerationSettingsSnapshot
{
	// Keep in sync with UDesign2UMGSettings::PrototypeNavigationGenerationMode (default Disabled).
	EDesign2UMGPrototypeNavigationGenerationMode PrototypeNavigationGenerationMode =
		EDesign2UMGPrototypeNavigationGenerationMode::Disabled;
	TArray<FWidgetClassOverride> ClassOverrides;

	/**
	 * Project-level Component Mapping config (the loaded result of Config/Design2UMGComponentMapping.ini).
	 * Carried into the generation chain with the import settings snapshot, lifetime tied to the snapshot, never a global;
	 * preview materialization (preview narrowing) carries no mapping.
	 */
	FDesign2UMGComponentMappingConfig ComponentMapping;

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

namespace Design2UMGGenerationSettingsContext
{
	const FDesign2UMGGenerationSettingsSnapshot* GetCurrent();

	class FScopedSettingsSnapshot
	{
	public:
		explicit FScopedSettingsSnapshot(const FDesign2UMGGenerationSettingsSnapshot& InSnapshot);
		~FScopedSettingsSnapshot();

	private:
		const FDesign2UMGGenerationSettingsSnapshot* PreviousSnapshot = nullptr;
		const FDesign2UMGGenerationSettingsSnapshot& Snapshot;
	};
}
