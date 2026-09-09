// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGGenerationSettingsSnapshot.h"

#include "Components/Widget.h"
#include "DesignPreviewContracts.h"

FDesign2UMGGenerationSettingsSnapshot FDesign2UMGGenerationSettingsSnapshot::FromSettings(const UDesign2UMGSettings& Settings)
{
	FDesign2UMGGenerationSettingsSnapshot Snapshot;
	Snapshot.PrototypeNavigationGenerationMode = Settings.PrototypeNavigationGenerationMode;
	Snapshot.ClassOverrides = Settings.ClassOverrides;
	// Read the project-level Component Mapping ini once per import request when building the snapshot; it flows into the generation chain with the snapshot.
	Snapshot.ComponentMapping = Design2UMGComponentMappingConfig::Load();
	return Snapshot;
}

// Single copy boundary for the preview narrowing step. When a new generation
// field must reach preview materialization, copy it here once.
FDesignPreviewMaterializationSettingsSnapshot FDesignPreviewMaterializationSettingsSnapshot::FromGenerationSettings(
	const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
{
	FDesignPreviewMaterializationSettingsSnapshot Snapshot;
	Snapshot.ClassOverrides = GenerationSettings.ClassOverrides;
	return Snapshot;
}

// Single copy boundary for the reverse (materialization -> generation) narrowing
// step. When a new generation field must round-trip through preview approval,
// copy it here once.
FDesign2UMGGenerationSettingsSnapshot FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(
	const FDesignPreviewMaterializationSettingsSnapshot& MaterializationSettings)
{
	FDesign2UMGGenerationSettingsSnapshot GenerationSettings;
	GenerationSettings.ClassOverrides = MaterializationSettings.ClassOverrides;
	return GenerationSettings;
}

UClass* FDesign2UMGGenerationSettingsSnapshot::FindClassOverride(
	const FString& NodeName,
	TMap<FString, UClass*>* InOutNodeNameCache) const
{
	return UDesign2UMGSettings::ResolveClassOverride(ClassOverrides, NodeName, InOutNodeNameCache);
}
