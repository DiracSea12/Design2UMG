// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenMCPBridge.h"
#include "DesignImportPlanning.h"
#include "DesignImportPipeline.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "PenParser.h"
#include "../../../Design2UMGCore/Private/PencilScript.h"
#include "DesignReferenceExpansion.h"
#include "PencilExtensionResources.h"
#include "UMGSchema.h"

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

	bool ValidateTargetNode(
		const TArray<TSharedPtr<FJsonValue>>& AllChildren,
		const FString& TargetNodeId)
	{
		if (TargetNodeId.IsEmpty() || FindNodeById(AllChildren, TargetNodeId).IsValid())
		{
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

		for (const TCHAR* Field : { TEXT("version"), TEXT("variables"), TEXT("themes"), TEXT("fonts"), TEXT("imports") })
		{
			if (SourceObj->HasField(Field))
				Doc->SetField(Field, SourceObj->TryGetField(Field));
		}

		// Keep the definition trees intact until parsed render-root selection.
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
		if (!DesignJsonUtils::TryParseArray(NodesJson, NodesArr))
		{
			UE_LOG(LogPenMCPBridge, Error, TEXT("Failed to parse NodesJson"));
			return false;
		}

		TSharedPtr<FJsonObject> VarsObj;
		if (!DesignJsonUtils::TryParseObject(VariablesJson, VarsObj))
		{
			UE_LOG(LogPenMCPBridge, Warning, TEXT("VariablesJson parse failed or empty, using defaults"));
			VarsObj = MakeShared<FJsonObject>();
		}

		if (!ValidateTargetNode(NodesArr, TargetNodeId))
		{
			return false;
		}

		if (!VarsObj->HasField(TEXT("variables")))
		{
			TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
			Wrapped->SetObjectField(TEXT("variables"), VarsObj);
			VarsObj = Wrapped;
		}

		OutDocumentObject = BuildDocumentObject(VarsObj, NodesArr);
		UE_LOG(LogPenMCPBridge, Log, TEXT("BuildDocumentJson: %d children"), NodesArr.Num());
		return OutDocumentObject.IsValid();
	}
}

bool FPenMCPBridge::ParseFromMCP(
	const FString& NodesJson,
	const FString& VariablesJson,
	FDesignDocument& OutDocument,
	const FString& TargetNodeId,
	const FString& SourceFilePath)
{
	OutDocument = FDesignDocument();
	TSharedPtr<FJsonObject> DocumentObject;
	if (!AssembleDocumentObject(NodesJson, VariablesJson, DocumentObject, TargetNodeId))
	{
		return false;
	}

	// Register extension resources before parsing the assembled in-memory document.
	PencilExtensionResources::EnsureIconGlyphsRegistered();
	PencilExtensionResources::EnsureBundledLibrariesRegistered();
	if (!FPenParser::ParseDocumentObject(DocumentObject, OutDocument, SourceFilePath))
	{
		return false;
	}
	PromoteSelectedNodeToRenderRoot(OutDocument, TargetNodeId);
	PencilExtensionResources::ReportPenResourceGaps(OutDocument);

	return true;
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
	FDesignDocument Document;
	if (!ParseFromMCP(NodesJson, VariablesJson, Document, TargetNodeId))
	{
		return false;
	}

	const FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeConversion(Document, SettingsSnapshot);
	if (!Analysis.ErrorMessage.IsEmpty()) return false;
	OutData = Analysis.Data;
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

	if (!ValidateTargetNode(Children, TargetNodeId))
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

	TMap<FString, TSharedPtr<FDesignNode>> NodesById;
	Document.BuildReferenceNodeIndex(NodesById);
	TSharedPtr<FDesignNode> Selected = NodesById.FindRef(TargetNodeId);
	if (!Selected.IsValid())
	{
		for (const auto& Pair : NodesById)
		{
			if (IsEquivalentNodeId(Pair.Key, TargetNodeId))
			{
				Selected = Pair.Value;
				break;
			}
		}
	}
	if (!Selected.IsValid()) return;

	Document.Children = { Selected };
	TArray<TSharedPtr<FDesignNode>>& Definitions = Document.MutableSourceDocument().ImportedNodes;
	TMap<FString, TSharedPtr<FDesignNode>> RetainedNodes;
	Document.BuildReferenceNodeIndex(RetainedNodes);
	// Libraries and reusable definitions keep their existing import semantics.
	for (const auto& Pair : NodesById)
	{
		if (Pair.Value->bReusable && !RetainedNodes.Contains(Pair.Key)) Definitions.Add(Pair.Value);
	}
	Document.BuildReferenceNodeIndex(RetainedNodes);

	TMap<const FDesignNode*, TSet<FString>> Visited;
	TSet<FString> ActiveReferences;
	// Keep expanded instances alive while their pointers identify dependency visits.
	TArray<TSharedPtr<FDesignNode>> Instances;
	using FTheme = TMap<FString, FString>;
	TFunction<void(const TSharedPtr<FDesignNode>&, FTheme)> VisitNode;
	TFunction<void(const FDesignNodePatch&, const FTheme&)> VisitPatch;
	auto KeepReference = [&](const FString& RefId, const FTheme& Theme)
	{
		const TSharedPtr<FDesignNode> Target = NodesById.FindRef(RefId);
		if (!Target.IsValid()) return;
		if (!RetainedNodes.Contains(RefId))
		{
			Definitions.Add(Target);
			RetainedNodes.Add(RefId, Target);
		}
		VisitNode(Target, Theme);
	};
	VisitPatch = [&](const FDesignNodePatch& Patch, const FTheme& Theme)
	{
		if (Patch.Ref.IsSet()) KeepReference(Patch.Ref.GetValue(), Theme);
		if (Patch.Children.IsSet())
			for (const auto& Child : Patch.Children.GetValue()) VisitNode(Child, Theme);
		for (const auto& Pair : Patch.Descendants)
		{
			if (!Pair.Value.IsValid()) continue;
			VisitNode(Pair.Value->Replacement, Theme);
			VisitPatch(Pair.Value->Patch, Theme);
		}
	};
	VisitNode = [&](const TSharedPtr<FDesignNode>& Node, FTheme Theme)
	{
		if (!Node.IsValid()) return;
		for (const auto& Pair : Node->Theme) Theme.Add(Pair.Key, Pair.Value);
		// Match conversion's canonical theme signature: sorted axes with length-delimited values.
		TArray<FString> ThemeKeys;
		Theme.GetKeys(ThemeKeys);
		ThemeKeys.Sort();
		FString ThemeSignature;
		for (const FString& Key : ThemeKeys)
		{
			ThemeSignature += FString::Printf(TEXT("%d:%s=%d:%s;"),
				Key.Len(), *Key, Theme.FindChecked(Key).Len(), *Theme.FindChecked(Key));
		}
		TSet<FString>& VisitedThemes = Visited.FindOrAdd(Node.Get());
		if (VisitedThemes.Contains(ThemeSignature)) return;
		VisitedThemes.Add(ThemeSignature);
		if (Node->Type == EDesignNodeType::Ref)
		{
			KeepReference(Node->Ref, Theme);
			if (!ActiveReferences.Contains(Node->Ref))
			{
				ActiveReferences.Add(Node->Ref);
				const auto Expansion = RefExpander::ExpandRef(Node, Document);
				if (Expansion.IsSuccess())
				{
					Instances.Add(Expansion.ExpandedNode);
					VisitNode(Expansion.ExpandedNode, Theme);
				}
				ActiveReferences.Remove(Node->Ref);
			}
		}
		if (Node->Type == EDesignNodeType::Script)
			for (const FString& Reference : PencilScript::CollectInputReferences(Node, Document, Theme)) KeepReference(Reference, Theme);
		for (const auto& Child : Node->Children) VisitNode(Child, Theme);
		if (Node->RefOverrides.IsValid()) VisitPatch(*Node->RefOverrides, Theme);
		for (const auto& Pair : Node->Descendants)
		{
			VisitNode(Pair.Value.Replacement, Theme);
			VisitPatch(Pair.Value.Patch, Theme);
		}
	};
	const TArray<TSharedPtr<FDesignNode>> SeedDefinitions = Definitions;
	VisitNode(Selected, {});
	for (const auto& Definition : SeedDefinitions) VisitNode(Definition, {});
	Selected->bReusable = false;
}

