// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenMCPBridge.h"
#include "DesignSourceRegistry.h"
#include "UMGSchema.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPenMCPBridge, Log, All);

namespace
{
	/** 鍦?JSON 鏁扮粍涓寜 id 鏌ユ壘鑺傜偣 */
	FString CanonicalizeNodeId(const FString& InId)
	{
		FString Id = InId.TrimStartAndEnd();
		const FString Lower = Id.ToLower();
		if (Lower.StartsWith(TEXT("frame-"))
			|| Lower.StartsWith(TEXT("node-"))
			|| Lower.StartsWith(TEXT("screen-"))
			|| Lower.StartsWith(TEXT("canvas-")))
		{
			int32 DashIndex = INDEX_NONE;
			if (Id.FindChar(TEXT('-'), DashIndex))
			{
				const FString Suffix = Id.Mid(DashIndex + 1);
				// 濡傛灉鍚庣紑闈炵┖锛岃繑鍥炲悗缂€锛涘惁鍒欒繑鍥炲師濮?ID
				return Suffix.IsEmpty() ? Id : Suffix;
			}
		}
		return Id;
	}

	bool IsEquivalentNodeId(const FString& NodeId, const FString& TargetId)
	{
		if (NodeId.Equals(TargetId, ESearchCase::CaseSensitive)
			|| NodeId.Equals(TargetId, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return CanonicalizeNodeId(NodeId).Equals(CanonicalizeNodeId(TargetId), ESearchCase::IgnoreCase);
	}

	TSharedPtr<FJsonValue> FindNodeById(const TArray<TSharedPtr<FJsonValue>>& Nodes, const FString& TargetId)
	{
		for (const auto& Node : Nodes)
		{
			const TSharedPtr<FJsonObject>& Obj = Node->AsObject();
			if (!Obj.IsValid())
				continue;

			FString NodeId;
			if (Obj->TryGetStringField(TEXT("id"), NodeId) && IsEquivalentNodeId(NodeId, TargetId))
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

	/** 鏀堕泦鎵€鏈?reusable 鑺傜偣锛堢粍浠跺畾涔夛級锛宺ef 灞曞紑鏃堕渶瑕?*/
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

	/** 鎸?TargetNodeId 杩囨护锛屽悓鏃朵繚鐣欐墍鏈?reusable 缁勪欢瀹氫箟 */
	TArray<TSharedPtr<FJsonValue>> FilterWithReusables(
		const TArray<TSharedPtr<FJsonValue>>& AllChildren,
		const FString& TargetNodeId)
	{
		TSharedPtr<FJsonValue> Found = FindNodeById(AllChildren, TargetNodeId);
		if (!Found.IsValid())
			return TArray<TSharedPtr<FJsonValue>>();

		// 鏀堕泦鎵€鏈?reusable 鑺傜偣
		TArray<TSharedPtr<FJsonValue>> Result;
		CollectReusableNodes(AllChildren, Result);

		// 鍘婚噸锛氬鏋滅洰鏍囪妭鐐规湰韬氨鏄?reusable锛屼笉瑕侀噸澶嶆坊鍔?
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

	/** 浠庢簮瀵硅薄缁勮鏍囧噯鏂囨。 JSON 骞跺簭鍒楀寲 */
	FString BuildDocumentJson(
		const TSharedPtr<FJsonObject>& SourceObj,
		const TArray<TSharedPtr<FJsonValue>>& Children)
	{
		TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();

		// 浼樺厛浣跨敤鍘熷鐗堟湰鍙凤紝鍥為€€鍒?"2.6"
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
	// 瑙ｆ瀽鑺傜偣鏁扮粍
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TSharedRef<TJsonReader<>> NodesReader = TJsonReaderFactory<>::Create(NodesJson);
	if (!FJsonSerializer::Deserialize(NodesReader, NodesArr))
	{
		UE_LOG(LogPenMCPBridge, Error, TEXT("Failed to parse NodesJson"));
		return TEXT("");
	}

	// 瑙ｆ瀽鍙橀噺瀵硅薄
	TSharedPtr<FJsonObject> VarsObj;
	TSharedRef<TJsonReader<>> VarsReader = TJsonReaderFactory<>::Create(VariablesJson);
	if (!FJsonSerializer::Deserialize(VarsReader, VarsObj) || !VarsObj.IsValid())
	{
		UE_LOG(LogPenMCPBridge, Warning, TEXT("VariablesJson parse failed or empty, using defaults"));
		VarsObj = MakeShared<FJsonObject>();
	}

	// 濡傛灉鎸囧畾浜?TargetNodeId锛岃繃婊わ紙淇濈暀 reusable 缁勪欢瀹氫箟锛?
	TArray<TSharedPtr<FJsonValue>> Children;
	if (!TargetNodeId.IsEmpty())
	{
		Children = FilterWithReusables(NodesArr, TargetNodeId);
		if (Children.Num() == 0)
		{
			const FString CanonicalId = CanonicalizeNodeId(TargetNodeId);
			UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found (hint: '%s' / 'frame-%s')"),
				*TargetNodeId, *CanonicalId, *CanonicalId);
			return TEXT("");
		}
	}
	else
	{
		Children = NodesArr;
	}

	// VarsObj 鍙兘鏄畬鏁存枃妗ｅ厓鏁版嵁锛堝惈 version/variables/themes/fonts锛夛紝
	// 涔熷彲鑳藉彧鏄彉閲忓璞℃湰韬€傜‘淇?BuildDocumentJson 鑳芥纭鍙栧悇瀛楁銆?
	TSharedPtr<FJsonObject> SourceObj = VarsObj;
	if (!VarsObj->HasField(TEXT("variables")))
	{
		// VarsObj 鏈韩灏辨槸鍙橀噺鍐呭锛屽寘瑁呬负鏂囨。绾у璞?
		SourceObj = MakeShared<FJsonObject>();
		SourceObj->SetObjectField(TEXT("variables"), VarsObj);
	}

	return BuildDocumentJson(SourceObj, Children);
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
	FString Json;
	if (!BuildFilteredDocumentJson(DocObj, Json, TargetNodeId))
		return false;

	return FDesignSourceRegistry::Convert(TEXT("Pen"), Json, OutData);
}

bool FPenMCPBridge::BuildFilteredDocumentJson(
	const TSharedPtr<FJsonObject>& DocObj,
	FString& OutDocumentJson,
	const FString& TargetNodeId)
{
	OutDocumentJson.Reset();
	if (!DocObj.IsValid())
		return false;

	// 鎻愬彇 children
	TArray<TSharedPtr<FJsonValue>> Children;
	const TArray<TSharedPtr<FJsonValue>>* ChildrenPtr = nullptr;
	if (DocObj->TryGetArrayField(TEXT("children"), ChildrenPtr) && ChildrenPtr)
	{
		Children = *ChildrenPtr;
	}

	// 鎸?TargetNodeId 杩囨护锛堜繚鐣?reusable 缁勪欢瀹氫箟锛?
	if (!TargetNodeId.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Filtered = FilterWithReusables(Children, TargetNodeId);
		if (Filtered.Num() == 0)
		{
			const FString CanonicalId = CanonicalizeNodeId(TargetNodeId);
			UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found (hint: '%s' / 'frame-%s')"),
				*TargetNodeId, *CanonicalId, *CanonicalId);
			return false;
		}
		Children = MoveTemp(Filtered);
	}

	OutDocumentJson = BuildDocumentJson(DocObj, Children);
	return !OutDocumentJson.IsEmpty();
}

