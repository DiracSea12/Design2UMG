// Copyright Epic Games, Inc. All Rights Reserved.

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

	// 如果指定了 TargetNodeId，过滤
	TArray<TSharedPtr<FJsonValue>> Children;
	if (!TargetNodeId.IsEmpty())
	{
		TSharedPtr<FJsonValue> Found = FindNodeById(NodesArr, TargetNodeId);
		if (!Found.IsValid())
		{
			UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found"), *TargetNodeId);
			return TEXT("");
		}
		Children.Add(Found);
	}
	else
	{
		Children = NodesArr;
	}

	// 组装文档
	TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();
	Doc->SetStringField(TEXT("version"), TEXT("2.6"));

	// variables
	if (VarsObj->HasField(TEXT("variables")))
		Doc->SetField(TEXT("variables"), VarsObj->TryGetField(TEXT("variables")));

	// themes
	if (VarsObj->HasField(TEXT("themes")))
		Doc->SetField(TEXT("themes"), VarsObj->TryGetField(TEXT("themes")));

	// fonts
	if (VarsObj->HasField(TEXT("fonts")))
		Doc->SetField(TEXT("fonts"), VarsObj->TryGetField(TEXT("fonts")));

	// children
	Doc->SetArrayField(TEXT("children"), Children);

	// 序列化
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Doc.ToSharedRef(), Writer);

	UE_LOG(LogPenMCPBridge, Log, TEXT("Assembled document: %d children, json length=%d"),
		Children.Num(), Output.Len());

	return Output;
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
