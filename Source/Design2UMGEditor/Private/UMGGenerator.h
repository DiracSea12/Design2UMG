// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

/** 一次生成的结构化输出：manifest 载荷（成功保存后写 sidecar）+ Component Mapping 诊断。 */
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
	 * 编译并保存 WBP；传入生成输出时在保存成功后把 manifest 原子写到资产同目录 sidecar。
	 * 保存失败不写 manifest（不允许半更新资产 + 新 manifest 的组合）；
	 * 资产保存成功但 sidecar 写失败时，导入仍按成功处理，写失败作为结构化诊断
	 * 追加进 GenerationOutput->MappingDiagnostics（进导入报告）。
	 */
	static bool SaveWidgetBlueprint(
		UWidgetBlueprint* WBP,
		FDesign2UMGGenerationOutput* GenerationOutput = nullptr);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * 测试注入（reimport 保存失败语义专用）：武装后下一次 SaveWidgetBlueprint 直接失败一次，
	 * 不触碰任何状态；用于确定性验证「事务回滚 / manifest 不更新」路径。生产路径不受影响。
	 */
	static void ArmAutomationSaveFailureOnce();
#endif
};

