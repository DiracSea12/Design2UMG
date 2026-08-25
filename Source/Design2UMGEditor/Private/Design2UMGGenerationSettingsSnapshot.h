// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"

class UClass;
struct FDesignPreviewMaterializationSettingsSnapshot;

struct FDesign2UMGGenerationSettingsSnapshot
{
	EDesign2UMGPrototypeNavigationGenerationMode PrototypeNavigationGenerationMode =
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension;
	TArray<FWidgetClassOverride> ClassOverrides;

	static FDesign2UMGGenerationSettingsSnapshot FromSettings(const UDesign2UMGSettings& Settings);
	static FDesign2UMGGenerationSettingsSnapshot FromMaterializationSettings(
		const FDesignPreviewMaterializationSettingsSnapshot& MaterializationSettings);
	UClass* FindClassOverride(const FString& NodeName) const;
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
