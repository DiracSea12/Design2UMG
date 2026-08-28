// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignComponentMapping.h"
#include "Design2UMGComponentMappingConfig.h"
#include "Components/Widget.h"
#include "UMGSchema.h"

class UUserWidget;
class UWidget;
class UWidgetTree;
struct FUMGPrototypeNavigationData;

/** manifest 里的 input 声明记录（source/target/valueKind 三元组）：只描述映射声明本身，
 *  不携带设计值或写入结果——reimport 按设计身份重新解析拿最新值，不靠 manifest 存值。 */
struct FDesign2UMGMappedInputRecord
{
	FString Source;
	FString Target;
	FString ValueKind;
};

/** manifest 里的 mapped 实例记录：designNodeId + widgetName 定位，同一组件定义的多实例互不混淆。 */
struct FDesign2UMGMappedInstanceRecord
{
	FString DesignNodeId;
	FString WidgetName;
	FString MappedKey;
	FString MappedClass;
	bool bMatchedAtSetLevel = false;
	TArray<FDesign2UMGMappedInputRecord> Inputs;
};

/** manifest 里的纯生成顶层节点记录。 */
struct FDesign2UMGGeneratedInstanceRecord
{
	FString DesignNodeId;
	FString WidgetName;
};

/**
 * 一次生成过程中的 Component Mapping 状态：
 * - 持有配置（匹配表），
 * - 记录命中的 IR 节点、创建出的 mapped 实例、顶层生成节点，
 * - 收集生成期诊断（命中/回退/inputs 写入/Prototype 不生效）。
 * 只在一次 BuildWidgetBlueprint 生命周期内存在，不是全局对象。
 */
class FDesign2UMGComponentMappingBuildState
{
public:
	explicit FDesign2UMGComponentMappingBuildState(const FDesign2UMGComponentMappingConfig& InConfig)
		: Config(&InConfig)
	{
	}

	/** 根 widget 构建开始前调用：标记下一个创建的 widget 就是页面宿主根。 */
	void BeginRootBuild()
	{
		bRootPending = true;
	}

	/** reimport 复用生成链时调用：页面宿主根已存在，直接指认，不再靠捕获下一个新 widget。 */
	void SetRootWidget(UWidget* Widget)
	{
		RootWidget = Widget;
		bRootPending = false;
	}

	/** 每个由生成链创建的 widget 都在这里过一遍：用于捕获根 widget（在子节点构建前生效）。 */
	void OnWidgetConstructed(UWidget* ConstructedWidget)
	{
		if (bRootPending && ConstructedWidget)
		{
			RootWidget = ConstructedWidget;
			bRootPending = false;
		}
	}

	/** mapped 节点是否命中（IR 标记由 ApplyMappingToWidgetTreeIR 写入）。 */
	bool IsMappedNode(const FUMGWidgetNode& Node) const;

	const DesignComponentMapping::FComponentMappingEntry* FindEntryForKey(const FString& Key) const
	{
		return Config->Table.FindEntryByKey(Key);
	}

	/** IR 节点相对 mapped 组件的标记：本体命中（含匹配级别）或只是命中子树内部的节点。 */
	struct FMappedNodeMark
	{
		/** true = 节点本体命中 mapping（是 mapped 实例）；false = 只是 mapped 子树内部节点。 */
		bool bMappedInstance = false;
		/** 本体命中发生在组件集级；仅 bMappedInstance 时有意义。 */
		bool bMatchedAtSetLevel = false;
	};

	/** 本体命中：登记 mapped 实例节点（含匹配级别）。 */
	void AddMappedNodeId(const FString& NodeId, bool bMatchedAtSetLevel)
	{
		FMappedNodeMark& Mark = MappedNodes.FindOrAdd(NodeId);
		Mark.bMappedInstance = true;
		Mark.bMatchedAtSetLevel = bMatchedAtSetLevel;
	}

	/** 子树内部：mapped 节点不再展开的内部节点（嵌套命中的实例标记不被覆盖）。 */
	void AddMappedSubtreeNodeId(const FString& NodeId)
	{
		MappedNodes.FindOrAdd(NodeId);
	}

	bool IsMappedNodeId(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && Mark->bMappedInstance;
	}

	/** 触发点是否位于 mapped 子树内部（不是实例本体）。 */
	bool IsInsideMappedSubtree(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && !Mark->bMappedInstance;
	}

	bool IsMappedAtSetLevel(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && Mark->bMappedInstance && Mark->bMatchedAtSetLevel;
	}

	void AddDiagnostic(const FString& Kind, const FString& Message)
	{
		FDesign2UMGMappingDiagnostic Diagnostic;
		Diagnostic.Kind = Kind;
		Diagnostic.Message = Message;
		Diagnostics.Add(MoveTemp(Diagnostic));
	}

	/** mapped 实例创建时登记（inputs 之后按 widgetName 回填）。 */
	void RecordMappedInstance(const FDesign2UMGMappedInstanceRecord& Record)
	{
		MappedInstances.Add(Record);
	}

	FDesign2UMGMappedInstanceRecord* FindMappedInstanceByWidgetName(const FString& WidgetName)
	{
		for (FDesign2UMGMappedInstanceRecord& Record : MappedInstances)
		{
			if (Record.WidgetName == WidgetName)
			{
				return &Record;
			}
		}
		return nullptr;
	}

	/** 记录顶层生成节点（根 widget 的直属子节点）；mapped 节点已在 RecordMappedInstance 登记，这里跳过。 */
	void RecordCreatedWidget(const FUMGWidgetNode& Node, const UWidget* CreatedWidget, const UWidget* ParentWidget);

	bool IsRootWidget(const UWidget* Widget) const
	{
		return Widget && RootWidget.Get() == Widget;
	}

	/** 本次生成链创建的全部 widget 名（含嵌套叶子；reimport 的 manifest.d2uOwned 数据源）。 */
	const TSet<FString>& GetCreatedWidgetNames() const { return CreatedWidgetNames; }

	const TArray<FDesign2UMGMappedInstanceRecord>& GetMappedInstances() const { return MappedInstances; }
	const TArray<FDesign2UMGGeneratedInstanceRecord>& GetTopLevelGenerated() const { return TopLevelGenerated; }
	const TArray<FDesign2UMGMappingDiagnostic>& GetDiagnostics() const { return Diagnostics; }

private:
	const FDesign2UMGComponentMappingConfig* Config = nullptr;
	TWeakObjectPtr<UWidget> RootWidget;
	bool bRootPending = false;
	/** 单一容器同时记录 mapped 实例本体与命中子树内部节点（键 = IR 节点 Id）。 */
	TMap<FString, FMappedNodeMark> MappedNodes;
	/** 本次创建的全部 widget 名（除页面宿主根），所有权记录，不区分顶层/嵌套。 */
	TSet<FString> CreatedWidgetNames;
	TArray<FDesign2UMGMappedInstanceRecord> MappedInstances;
	TArray<FDesign2UMGGeneratedInstanceRecord> TopLevelGenerated;
	TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
};

namespace Design2UMGComponentMappingGeneration
{
	/**
	 * 生成前对 IR 做映射匹配：解析身份 → 两级匹配 → 把命中的 mapping Key 写进节点源数据边车。
	 * 未命中/无配置/非 Figma 文档全部原样通过；同时在命中时输出 scope 与命中诊断。
	 */
	void ApplyMappingToWidgetTreeIR(
		FUMGWidgetNode& RootNode,
		const FDesign2UMGComponentMappingConfig& Config,
		const FUMGWidgetData& WidgetData,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * 为命中 mapping 的 IR 节点创建项目 WBP 类实例（挂入 WidgetTree，命名沿用去重后的名字）。
	 * 类加载失败时记录诊断并返回 nullptr，调用方回退普通转换路径；组件内部子树由调用方不再展开。
	 */
	UUserWidget* TryCreateMappedWidget(
		UWidgetTree* WidgetTree,
		const FUMGWidgetNode& Node,
		const FName& WidgetName,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * 把显式 design-facing inputs 按声明写入 mapped 实例（反射查找 Target：WBP 类属性 → UWidget 继承属性）。
	 * 单个 input 失败只跳过该 input 并记录诊断，其余 inputs 与映射本身继续生效。
	 */
	void ApplyMappedComponentInputs(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * 上面的无 BuildState 版本，reimport 复用：按 mapping 声明把最新设计值直接覆写进既有实例
	 * （inputs 属 D2U-owned，不读项目侧当前值、不记基线）。逐 input 输出覆写/跳过诊断；
	 * OutInputRecords（可选）回填与导入 manifest 相同结构的 inputs 声明记录。
	 */
	void ApplyMappedComponentInputsToInstance(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		TArray<FDesign2UMGMappedInputRecord>* OutInputRecords,
		TArray<FDesign2UMGMappingDiagnostic>& OutDiagnostics);

	/** 设计交互挂在 mapped 身份上时输出「不生效」诊断（v1 不接管 mapped WBP 内部交互）。
	 *  OutDiagnostics（可选）同步接收这些条目：reimport 不读 BuildState 的生成期诊断，
	 *  需要把专门条目显式带进 reimport 报告时传入。 */
	void ReportInertPrototypeBindings(
		const FUMGPrototypeNavigationData& NavigationData,
		FDesign2UMGComponentMappingBuildState& BuildState,
		TArray<FDesign2UMGMappingDiagnostic>* OutDiagnostics = nullptr);
}
