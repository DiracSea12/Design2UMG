// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGSettings.h"
#include "Design2UMGComponentMappingConfig.h"

class UClass;
struct FDesignPreviewMaterializationSettingsSnapshot;

struct FDesign2UMGGenerationSettingsSnapshot
{
	EDesign2UMGPrototypeNavigationGenerationMode PrototypeNavigationGenerationMode =
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension;
	TArray<FWidgetClassOverride> ClassOverrides;

	/**
	 * 项目级 Component Mapping 配置（Config/Design2UMGComponentMapping.ini 的加载结果）。
	 * 随导入设置快照传入生成链，生命周期与快照一致，不落全局对象；
	 * 预览物化（preview narrowing）不带 mapping。
	 */
	FDesign2UMGComponentMappingConfig ComponentMapping;

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
