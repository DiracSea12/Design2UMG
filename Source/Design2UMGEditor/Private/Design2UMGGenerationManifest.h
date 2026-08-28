// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGComponentMappingGeneration.h"

class UWidgetBlueprint;

/**
 * sidecar 实例 kind 与「sidecar 写失败」诊断 kind 的规范值。
 * 写端（GenerationManifest 序列化 / UMGGenerator 诊断）与读端（Reimport 对账与
 * 保存成败判定）共用这一份常量：这些字符串是跨文件协议，任一端改字面量都会
 * 静默破坏对账（如 reimport 把 sidecar 写失败当成功汇报）。
 */
namespace Design2UMGManifestKinds
{
	constexpr const TCHAR* Mapped = TEXT("mapped");
	constexpr const TCHAR* Generated = TEXT("generated");
	constexpr const TCHAR* ManifestWriteFailedDiagnostic = TEXT("manifest_write_failed");
}


/**
 * 最小生成记录（sidecar manifest）载荷：
 * schemaVersion/provider/documentScope/sourceRoot/instances(kind=mapped|generated)/d2uOwned。
 * 由 BuildWidgetBlueprint 组装，导入/reimport 成功保存资产后原子写入 <AssetName>.d2u.json。
 * 属性值不进 manifest：reimport 的属性更新靠「按 designNodeId 重新转换后对照」，
 * inputs 记录只保留声明（source/target/valueKind），不存设计值。
 */
struct FDesign2UMGGenerationManifestPayload
{
	FString Provider;
	FString DocumentScope;
	FString SourceRootNodeId;
	FString SourceRootName;
	TArray<FDesign2UMGMappedInstanceRecord> MappedInstances;
	TArray<FDesign2UMGGeneratedInstanceRecord> TopLevelGenerated;
	/** 全部由 D2U 创建的 widget 名（含嵌套叶子），reimport 用它做删除/移动的所有权判定。 */
	TArray<FString> D2UOwnedNames;
};

/** 从 sidecar 反序列化出的生成记录（reimport 管线的输入）。 */
struct FDesign2UMGGenerationManifestSidecar
{
	int32 SchemaVersion = 0;
	FString Provider;
	FString DocumentScope;
	FString SourceRootNodeId;
	FString SourceRootName;

	/** 单条实例记录：kind = "mapped" | "generated"。 */
	struct FInstanceRecord
	{
		FString DesignNodeId;
		FString WidgetName;
		FString Kind;
		FString MappedKey;
		FString MappedClass;
		bool bMatchedAtSetLevel = false;
		TArray<FDesign2UMGMappedInputRecord> Inputs;
	};

	TArray<FInstanceRecord> Instances;
	TArray<FString> D2UOwned;

	const FInstanceRecord* FindInstanceByDesignNodeId(const FString& DesignNodeId) const
	{
		for (const FInstanceRecord& Record : Instances)
		{
			if (Record.DesignNodeId == DesignNodeId)
			{
				return &Record;
			}
		}
		return nullptr;
	}
};

namespace Design2UMGGenerationManifest
{
	/** manifest sidecar 文件名后缀（<AssetName>.d2u.json）。 */
	FString BuildSidecarPathForAsset(const FString& AssetPackageName);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * 测试注入：武装后下一次 WriteSidecarForAsset 直接失败一次，不触碰任何文件。
	 * 用于确定性验证 reimport 的「uasset 已保存但 manifest 未更新」传导路径
	 * （真实只读 sidecar 会被 reimport 的保存前 pre-check 拦住，测不到该分支）。
	 */
	void ArmAutomationSidecarWriteFailureOnce();
#endif

	/**
	 * 导入成功保存资产后调用：把 manifest 原子写到资产同目录（先写临时文件再替换），
	 * 失败不落半截文件。返回是否写入成功。
	 */
	bool WriteSidecarForAsset(
		const FString& AssetPackageName,
		const FDesign2UMGGenerationManifestPayload& Payload);

	/**
	 * reimport 前读取并解析资产同目录的 sidecar。文件缺失/JSON 非法/schema 不识别时
	 * 返回 false 并给出可读原因（进冲突诊断）。inputs 里的历史 value 字段一律忽略。
	 */
	bool TryLoadSidecarForAsset(
		const FString& AssetPackageName,
		FDesign2UMGGenerationManifestSidecar& OutSidecar,
		FString& OutError);
}
