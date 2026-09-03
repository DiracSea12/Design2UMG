// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenMCPBridge.h"
#include "Design2UMGEditorDesignSourceRegistry.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "PenParser.h"
#include "PencilExtensionResources.h"
#include "UMGSchema.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogPenMCPBridge, Log, All);

namespace
{
	/** Find a node by id inside a JSON array. */
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
				// Return the suffix when the prefixed form is used.
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
			if (Obj->TryGetArrayField(TEXT("children"), Children))
			{
				TSharedPtr<FJsonValue> FoundInChildren = FindNodeById(*Children, TargetId);
				if (FoundInChildren.IsValid())
					return FoundInChildren;
			}
		}
		return nullptr;
	}

	/** Collect reusable component definitions needed for ref expansion. */
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
			if (Obj->TryGetArrayField(TEXT("children"), Children))
			{
				CollectReusableNodes(*Children, OutReusable);
			}
		}
	}

	bool BuildFilteredChildren(
		const TArray<TSharedPtr<FJsonValue>>& AllChildren,
		const FString& TargetNodeId,
		TArray<TSharedPtr<FJsonValue>>& OutChildren)
	{
		if (TargetNodeId.IsEmpty())
		{
			OutChildren = AllChildren;
			return true;
		}

		TSharedPtr<FJsonValue> Found = FindNodeById(AllChildren, TargetNodeId);
		if (Found.IsValid())
		{
			// Keep reusable definitions so downstream ref expansion still works.
			TArray<TSharedPtr<FJsonValue>> FilteredChildren;
			CollectReusableNodes(AllChildren, FilteredChildren);

			// Deduplicate when the target node itself is reusable.
			FString TargetId;
			if (const TSharedPtr<FJsonObject>& FoundObj = Found->AsObject())
				FoundObj->TryGetStringField(TEXT("id"), TargetId);

			for (const auto& ReusableNode : FilteredChildren)
			{
				FString ReusableId;
				if (const TSharedPtr<FJsonObject>& ReusableObj = ReusableNode->AsObject())
					ReusableObj->TryGetStringField(TEXT("id"), ReusableId);
				if (ReusableId == TargetId)
				{
					OutChildren = MoveTemp(FilteredChildren);
					return true;
				}
			}

			FilteredChildren.Add(Found);
			OutChildren = MoveTemp(FilteredChildren);
			return true;
		}

		const FString CanonicalId = CanonicalizeNodeId(TargetNodeId);
		UE_LOG(LogPenMCPBridge, Error, TEXT("TargetNodeId '%s' not found (hint: '%s' / 'frame-%s')"),
			*TargetNodeId, *CanonicalId, *CanonicalId);
		return false;
	}

	TSharedPtr<FJsonObject> BuildDocumentObject(
		const TSharedPtr<FJsonObject>& SourceObj,
		const TArray<TSharedPtr<FJsonValue>>& Children)
	{
		TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();

		// Prefer the original version when it exists, otherwise fall back to 2.6.
		FString Version = TEXT("2.6");
		if (SourceObj->HasField(TEXT("version")))
		{
			SourceObj->TryGetStringField(TEXT("version"), Version);
		}
		Doc->SetStringField(TEXT("version"), Version);

		for (const TCHAR* Field : { TEXT("variables"), TEXT("themes"), TEXT("fonts"), TEXT("imports") })
		{
			if (SourceObj->HasField(Field))
				Doc->SetField(Field, SourceObj->TryGetField(Field));
		}

		Doc->SetArrayField(TEXT("children"), Children);
		return Doc;
	}

	bool AssembleDocumentObject(
		const FString& NodesJson,
		const FString& VariablesJson,
		TSharedPtr<FJsonObject>& OutDocumentObject,
		const FString& TargetNodeId)
	{
		OutDocumentObject.Reset();

		TArray<TSharedPtr<FJsonValue>> NodesArr;
		TSharedRef<TJsonReader<>> NodesReader = TJsonReaderFactory<>::Create(NodesJson);
		if (!FJsonSerializer::Deserialize(NodesReader, NodesArr))
		{
			UE_LOG(LogPenMCPBridge, Error, TEXT("Failed to parse NodesJson"));
			return false;
		}

		TSharedPtr<FJsonObject> VarsObj;
		TSharedRef<TJsonReader<>> VarsReader = TJsonReaderFactory<>::Create(VariablesJson);
		if (!FJsonSerializer::Deserialize(VarsReader, VarsObj) || !VarsObj.IsValid())
		{
			UE_LOG(LogPenMCPBridge, Warning, TEXT("VariablesJson parse failed or empty, using defaults"));
			VarsObj = MakeShared<FJsonObject>();
		}

		TArray<TSharedPtr<FJsonValue>> Children;
		if (!BuildFilteredChildren(NodesArr, TargetNodeId, Children))
		{
			return false;
		}

		if (!VarsObj->HasField(TEXT("variables")))
		{
			TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
			Wrapped->SetObjectField(TEXT("variables"), VarsObj);
			VarsObj = Wrapped;
		}

		OutDocumentObject = BuildDocumentObject(VarsObj, Children);
		UE_LOG(LogPenMCPBridge, Log, TEXT("BuildDocumentJson: %d children"), Children.Num());
		return OutDocumentObject.IsValid();
	}
}

bool FPenMCPBridge::ConvertFromMCP(
	const FString& NodesJson,
	const FString& VariablesJson,
	FUMGWidgetData& OutData,
	const FString& TargetNodeId)
{
	const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
		FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
	return ConvertFromMCP(NodesJson, VariablesJson, SettingsSnapshot, OutData, TargetNodeId);
}

bool FPenMCPBridge::ConvertFromMCP(
	const FString& NodesJson,
	const FString& VariablesJson,
	const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
	FUMGWidgetData& OutData,
	const FString& TargetNodeId)
{
	TSharedPtr<FJsonObject> DocumentObject;
	if (!AssembleDocumentObject(NodesJson, VariablesJson, DocumentObject, TargetNodeId))
	{
		return false;
	}

	// Parse the assembled DOM directly (no DOM->string->DOM round trip), then run the
	// same analysis the string-based source registry path performs.
	PencilExtensionResources::EnsureIconGlyphsRegistered();
	FDesignDocument Document;
	if (!FPenParser::ParseDocumentObject(DocumentObject, Document))
	{
		return false;
	}
	PromoteSelectedNodeToRenderRoot(Document, TargetNodeId);
	PencilExtensionResources::ReportPenResourceGaps(Document);

	FImportAnalysisInput AnalysisInput;
	AnalysisInput.bResolveImages = false;
	AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
	OutData = DesignImportPipeline::AnalyzeImport(Document, AnalysisInput).Data;
	return true;
}

bool FPenMCPBridge::BuildFilteredDocumentObject(
	const TSharedPtr<FJsonObject>& DocObj,
	TSharedPtr<FJsonObject>& OutDocumentObject,
	const FString& TargetNodeId)
{
	OutDocumentObject.Reset();
	if (!DocObj.IsValid())
		return false;

	// Extract top-level children from the source document.
	TArray<TSharedPtr<FJsonValue>> Children;
	const TArray<TSharedPtr<FJsonValue>>* ChildrenPtr = nullptr;
	if (DocObj->TryGetArrayField(TEXT("children"), ChildrenPtr))
	{
		Children = *ChildrenPtr;
	}

	if (!BuildFilteredChildren(Children, TargetNodeId, Children))
	{
		return false;
	}

	OutDocumentObject = BuildDocumentObject(DocObj, Children);
	return OutDocumentObject.IsValid();
}

void FPenMCPBridge::PromoteSelectedNodeToRenderRoot(FDesignDocument& Document, const FString& TargetNodeId)
{
	if (TargetNodeId.IsEmpty())
	{
		return;
	}

	for (const TSharedPtr<FDesignNode>& Child : Document.Children)
	{
		if (!Child.IsValid() || !IsEquivalentNodeId(Child->Id, TargetNodeId))
		{
			continue;
		}
		if (Child->bReusable)
		{
			Child->bReusable = false;
			UE_LOG(LogPenMCPBridge, Log,
				TEXT("Selected node '%s' is a reusable component; treating it as the render root for this import."),
				*Child->Id);
		}
		return;
	}
}

