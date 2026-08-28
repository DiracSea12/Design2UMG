// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGSourceNodeData;

/**
 * 纯逻辑的组件身份与项目级 Component Mapping 匹配层。
 *
 * 这里只做字符串层面的工作：身份提取、DocumentScope 解析、mapping 声明解析
 * 和两级匹配。类加载与实例化只能发生在 Editor 模块，本层不依赖任何 UE 资产。
 */
namespace DesignComponentMapping
{
	/** provider 规范值（Core 全仓共享）：source.provider 元数据、IR 文档边车与 mapping Key 的 Provider 段统一用这两个值。 */
	constexpr const TCHAR* ProviderFigma = TEXT("figma");
	constexpr const TCHAR* ProviderPencil = TEXT("pencil");

	/** v1 封闭的输入类型集合：Text(FText) / Bool(bool) / Name(FName) / Float(double)。 */
	enum class EComponentInputType : uint8
	{
		Text,
		Bool,
		Name,
		Float
	};

	/** v1 封闭的取值来源：组件自身文本 / componentProperties 键值。 */
	enum class EComponentInputValueKind : uint8
	{
		ComponentProperty,
		ComponentText
	};

	/** 单条显式 design-facing input 声明（已解析、已校验）。 */
	struct FComponentInputDeclaration
	{
		FString Source;
		FString Target;
		EComponentInputType Type = EComponentInputType::Text;
		EComponentInputValueKind ValueKind = EComponentInputValueKind::ComponentProperty;
	};

	/** 一条项目级 mapping 声明（Key/类路径已校验格式，类是否可加载由 Editor 侧校验）。 */
	struct FComponentMappingEntry
	{
		FString Key;
		FString Provider;
		FString DocumentScope;
		FString ComponentKey;
		FString WidgetClass;
		TArray<FComponentInputDeclaration> Inputs;
	};

	/** 规范组件身份：Provider + DocumentScope + ComponentKey（可选 ComponentSetKey）。 */
	struct FDesignComponentIdentity
	{
		FString Provider;
		FString DocumentScope;
		FString ComponentKey;
		FString ComponentSetKey;

		bool IsValid() const
		{
			return !Provider.IsEmpty() && !DocumentScope.IsEmpty() && !ComponentKey.IsEmpty();
		}

		/** variant 级匹配键：Provider|DocumentScope|ComponentKey。 */
		DESIGN2UMGCORE_API FString BuildVariantKey() const;
		/** 组件集级匹配键：Provider|DocumentScope|ComponentSetKey。 */
		DESIGN2UMGCORE_API FString BuildSetKey() const;
	};

	/** DocumentScope 解析结果；Source 说明本次 scope 用的是哪个来源，可进诊断。 */
	struct FDocumentScopeResolution
	{
		FString Value;
		FString Source; // "fileKey" | "importSource" | "unresolved"

		bool IsResolved() const { return !Value.IsEmpty(); }
	};

	/** DocumentScope 的统一解析规则（全仓库只此一处）：
	 *  Figma 导出 JSON 顶层带 file key 就用 file key；否则用导入源标识（URL / 文件路径）。 */
	DESIGN2UMGCORE_API FDocumentScopeResolution ResolveFigmaDocumentScope(
		const FString& DocumentFileKey,
		const FString& ImportSourceIdentifier);

	/**
	 * 从 UMG 侧源数据边车提取 Figma 组件身份。
	 * 判定规则：实例节点取 component 身份（mainComponentKey 优先，缺省用 componentId）；
	 * 组件定义节点取自身 id 作为 ComponentKey；ComponentSetKey 一律取 componentSetId。
	 * 提取不出非空 ComponentKey 时返回 false（不构成 mapped 候选）。
	 */
	DESIGN2UMGCORE_API bool TryResolveFigmaComponentIdentity(
		const FUMGSourceNodeData& SourceNode,
		const FString& DocumentScope,
		FDesignComponentIdentity& OutIdentity);

	/** 两级匹配表。先按 variant 级 Key 精确匹配；未命中且 ComponentSetKey 非空时按组件集级 Key 匹配。不做任何模糊匹配。 */
	struct FComponentMappingTable
	{
		TArray<FComponentMappingEntry> Entries;

		bool IsEmpty() const { return Entries.Num() == 0; }

		/** 返回命中的条目；bOutMatchedAtSetLevel 说明命中发生在组件集级。未命中返回 nullptr。 */
		DESIGN2UMGCORE_API const FComponentMappingEntry* Match(
			const FDesignComponentIdentity& Identity,
			bool& bOutMatchedAtSetLevel) const;

		DESIGN2UMGCORE_API const FComponentMappingEntry* FindEntryByKey(const FString& Key) const;
	};

	/** 解析 mapping Key 串（Provider|DocumentScope|ComponentKey）。三段都必须非空才算合法。 */
	DESIGN2UMGCORE_API bool ParseMappingKey(
		const FString& Key,
		FString& OutProvider,
		FString& OutDocumentScope,
		FString& OutComponentKey);

	/** 输入类型名与封闭集合的互转；非法字符串返回 false。 */
	DESIGN2UMGCORE_API bool ParseComponentInputType(const FString& Type, EComponentInputType& OutType);
	DESIGN2UMGCORE_API FString GetComponentInputTypeName(EComponentInputType Type);

	/** 取值来源名与封闭集合的互转；非法字符串返回 false。 */
	DESIGN2UMGCORE_API bool ParseComponentInputValueKind(
		const FString& ValueKind,
		EComponentInputValueKind& OutValueKind);
	DESIGN2UMGCORE_API FString GetComponentInputValueKindName(EComponentInputValueKind ValueKind);

	/**
	 * 解析并校验一条 input 声明。Source/Target 非空、Type/ValueKind 属于封闭集合才算合法；
	 * 非法时返回 false 并给出可读原因（进诊断）。
	 */
	DESIGN2UMGCORE_API bool TryParseComponentInputDeclaration(
		const FString& Source,
		const FString& Target,
		const FString& Type,
		const FString& ValueKind,
		FComponentInputDeclaration& OutDeclaration,
		FString& OutError);
}
