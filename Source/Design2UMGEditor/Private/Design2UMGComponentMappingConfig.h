// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignComponentMapping.h"
#include "Design2UMGComponentMappingConfig.generated.h"

/** Component Mapping 诊断条目：kind 分类 + 人类可读信息，最终进导入诊断报告。 */
struct FDesign2UMGMappingDiagnostic
{
	FString Kind;
	FString Message;
};

/** ini 里单条 input 声明的原始行结构，交给 UE config 系统解析。 */
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

/** ini 里单条 mapping 声明的原始行结构，交给 UE config 系统解析。 */
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
 * 项目级 Component Mapping 配置的加载结果：已校验的匹配表 + 校验诊断。
 * 作为导入请求生命周期内的普通值随生成设置快照传入生成链，不落任何全局对象。
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
	 * 读取并校验项目 Config/Design2UMGComponentMapping.ini 的 [/Design2UMG/ComponentMapping] 段。
	 * ini 不存在视为未配置（正常情况，静默返回空表）；存在时做全量校验：
	 * Key 三段格式、Provider 必须是 figma（Pencil 不进 v1 主路径）、WidgetClass 可解析且为
	 * UUserWidget 子类、Inputs 声明属于封闭集合。非法条目/非法 input 丢弃并出诊断。
	 * IniPath 参数仅供测试注入临时 ini，生产路径走项目 Config。
	 */
	FDesign2UMGComponentMappingConfig Load(const FString& IniPathOverride = FString());
}
