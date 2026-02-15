// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenMCPBridge.h"
#include "DesignSourceRegistry.h"
#include "UMGSchema.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPenMCPBridge, Log, All);

namespace
{
	/** 在 JSON 数组中按 id 查找节点 */
	TSharedPtr<FJsonValue> FindNodeById(const TArray<TSharedPtr<FJsonValue>>& Nodes, const FString& TargetId)
	{
		for (const auto& Node : Nodes)
		{
			const TSharedPtr<FJsonObject>& Obj = Node->AsObject();
			if (!Obj.IsValid())
				continue;

			FString NodeId;
			if (Obj->TryGetStringField(TEXT("id"), NodeId) && NodeId == TargetId)
				return Node;

			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Obj->TryGetArrayField(TEXT("children"), Children) && Children != nullptr)
			{
				TSharedPtr<FJsonValue> FoundInChildren = FindNodeById(*Children, TargetId);
				if (FoundInChildren.IsValid())
					return FoundInChildren;
			}
		}
		return nullptr;
	}

	/** 收集所有 reusable 节点（组件定义），ref 展开时需要 */
	void CollectReusableNodes(const TArray<TSharedPtr<FJsonValue>>& Nodes, TArray<TSharedPtr<FJsonValue>>& OutReusable)
	{
		for (const auto& Node : Nodes)
		{
			const TSharedPtr<FJsonObject>& Obj = Node->AsObject();
			if (!Obj.IsValid())
				continue;

			bool bReusable = false;
			if (Obj->TryGetBoolField(TEXT("reusable"), bReusable) && bReusable)
			{
				OutReusable.Add(Node);
			}

			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Obj->TryGetArrayField(TEXT("children"), Children) && Children != nullptr)
			{
				CollectReusableNodes(*Children, OutReusable);
			}
		}
	}

	/** 按 TargetNodeId 过滤，同时保留所有 reusable 组件定义 */
	TArray<TSharedPtr<FJsonValue>> FilterWithReusables(
		const TArray<TSharedPtr<FJsonValue>>& AllChildren,
		const FString& TargetNodeId)
	{
		TSharedPtr<FJsonValue> Found = FindNodeById(AllChildren, TargetNodeId);
		if (!Found.IsValid())
			return TArray<TSharedPtr<FJsonValue>>();

		// 收集所有 reusable 节点
		TArray<TSharedPtr<FJsonValue>> Result;
		CollectReusableNodes(AllChildren, Result);

		// 去重：如果目标节点本身就是 reusable，不要重复添加
		FString TargetId;
		if (const TSharedPtr<FJsonObject>& FoundObj = Found->AsObject())
			FoundObj->TryGetStringField(TEXT("id"), TargetId);

		bool bAlreadyIncluded = false;
		for (const auto& R : Result)
		{
			FString RId;
			if (const TSharedPtr<FJsonObject>& RObj = R->AsObject())
				RObj->TryGetStringField(TEXT("id"), RId);
			if (RId == TargetId)
			{
				bAlreadyIncluded = true;
				break;
			}
		}

		if (!bAlreadyIncluded)
			Result.Add(Found);

		return Result;
	}

	/** 从源对象组装标准文档 JSON 并序列化 */
	FString BuildDocumentJson(
		const TSharedPtr<FJsonObject>& SourceObj,
		const TArray<TSharedPtr<FJsonValue>>& Children)
	{
		TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();

		// 优先使用原始版本号，回退到 "2.6"
		FString Version = TEXT("2.6");
		if (SourceObj->HasField(TEXT("version")))
		{
			SourceObj->TryGetStringField(TEXT("version"), Version);
		}
		Doc->SetStringField(TEXT("version"), Version);

		for (const TCHAR* Field : { TEXT("variables"), TEXT("themes"), TEXT("fonts") })
		{
			if (SourceObj->HasField(Field))
				Doc->SetField(Field, SourceObj->TryGetField(Field));
		}

		Doc->SetArrayField(TEXT("children"), Children);

		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Doc.ToSharedRef(), Writer);

		UE_LOG(LogPenMCPBridge, Log, TEXT("BuildDocumentJson: %d children, json length=%d"),
			Children.Num(), Output.Len());

		return Output;
	}
}

FString FPenMCPBridge::AssembleDocumentJson(
	const FString& NodesJson,
	const FString& VariablesJson,
	const FString& TargetNodeId)
{
	// 解析节点数组
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TSharedRef<TJsonReader<>> NodesReader = TJsonReaderFactory<>::Create(NodesJson);
	if (!FJsonSerializer::Deserialize(NodesReader, NodesArr))
	{
		UE_LOG(LogPenMCPBridge, Error, TEXT("Failed to parse NodesJson"));
		return TEXT("");
	}

	// 解析变量对象
	TSharedPtr<FJsonObject> VarsObj;
	TSharedRef<TJsonReader<>> VarsReader = TJsonReaderFactory<>::Create(VariablesJson);
	if (!FJsonSerializer::Deserialize(VarsReader, VarsObj) || !VarsObj.IsValid())
	{
		UE_LOG(LogPenMCPBridge, Warning, TEXT("VariablesJson parse failed or empty, using defaults"));
		VarsObj = MakeShared<FJsonObject>();
	}

	// 如果指定了 TargetNodeId，过滤（保留 reusable 组件定义）
	TArray<TSharedPtr<FJsonValue>> Children;
	if (!TargetNodeId.IsEmpty())
	{
		Children = FilterWithReusables(NodesArr, TargetNodeId);
		if (Children.Num() == 0)
		{
			UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found"), *TargetNodeId);
			return TEXT("");
		}
	}
	else
	{
		Children = NodesArr;
	}

	return BuildDocumentJson(VarsObj, Children);
}

bool FPenMCPBridge::ConvertFromMCP(
	const FString& NodesJson,
	const FString& VariablesJson,
	FUMGWidgetData& OutData,
	const FString& TargetNodeId)
{
	FString Json = AssembleDocumentJson(NodesJson, VariablesJson, TargetNodeId);
	if (Json.IsEmpty())
		return false;

	return FDesignSourceRegistry::Convert(TEXT("Pen"), Json, OutData);
}

bool FPenMCPBridge::ConvertFromParsedDoc(
	const TSharedPtr<FJsonObject>& DocObj,
	FUMGWidgetData& OutData,
	const FString& TargetNodeId)
{
	if (!DocObj.IsValid())
		return false;

	// 提取 children
	TArray<TSharedPtr<FJsonValue>> Children;
	const TArray<TSharedPtr<FJsonValue>>* ChildrenPtr = nullptr;
	if (DocObj->TryGetArrayField(TEXT("children"), ChildrenPtr) && ChildrenPtr)
	{
		Children = *ChildrenPtr;
	}

	// 按 TargetNodeId 过滤（保留 reusable 组件定义）
	if (!TargetNodeId.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Filtered = FilterWithReusables(Children, TargetNodeId);
		if (Filtered.Num() == 0)
		{
			UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found"), *TargetNodeId);
			return false;
		}
		Children = MoveTemp(Filtered);
	}

	FString Json = BuildDocumentJson(DocObj, Children);
	if (Json.IsEmpty())
		return false;

	return FDesignSourceRegistry::Convert(TEXT("Pen"), Json, OutData);
}
