// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaComponentDependencies.h"
#include "DesignJsonUtils.h"
#include "DesignTreeLimits.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FigmaComponentDependencies
{
	namespace
	{
		bool FindNodeById(
			const TSharedPtr<FJsonObject>& Node,
			const FString& TargetId,
			TSharedPtr<FJsonObject>& OutNode,
			FDesignParserBudget& Budget,
			const int32 Depth,
			FString& OutError)
		{
			if (!Node || !Budget.TryEnterNode(Depth))
			{
				OutError = TEXT("Invalid Figma node or selected-root search exceeds parser limits");
				return false;
			}
			if (DesignJsonUtils::GetString(Node, TEXT("id")) == TargetId)
			{
				OutNode = Node;
				return true;
			}
			if (!Node->HasField(TEXT("children"))) return true;
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (!Node->TryGetArrayField(TEXT("children"), Children))
			{
				OutError = TEXT("Figma node children must be an array");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Child : *Children)
			{
				if (!Child || Child->Type != EJson::Object)
				{
					OutError = TEXT("Figma child must be an object");
					return false;
				}
				if (!FindNodeById(Child->AsObject(), TargetId, OutNode, Budget, Depth + 1, OutError)) return false;
				if (OutNode) return true;
			}
			return true;
		}

		bool CollectNodeIds(
			const TSharedPtr<FJsonObject>& Node,
			TSet<FString>& OutIds,
			FDesignParserBudget& Budget,
			const int32 Depth,
			FString& OutError)
		{
			if (!Node || !Budget.TryEnterNode(Depth))
			{
				OutError = TEXT("Invalid Figma node or selected subtree exceeds parser limits");
				return false;
			}
			const FString Id = DesignJsonUtils::GetString(Node, TEXT("id"));
			if (!Id.IsEmpty()) OutIds.Add(Id);
			if (!Node->HasField(TEXT("children"))) return true;
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (!Node->TryGetArrayField(TEXT("children"), Children))
			{
				OutError = TEXT("Figma node children must be an array");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Child : *Children)
			{
				if (!Child || Child->Type != EJson::Object
					|| !CollectNodeIds(Child->AsObject(), OutIds, Budget, Depth + 1, OutError))
				{
					if (OutError.IsEmpty()) OutError = TEXT("Figma child must be an object");
					return false;
				}
			}
			return true;
		}

		bool CollectReferences(const TSharedPtr<FJsonObject>& Node, TArray<FReference>& OutReferences,
			FDesignParserBudget& Budget, const int32 Depth, FString& Error)
		{
			if (!Node || !Budget.TryEnterNode(Depth))
			{
				Error = TEXT("Invalid component node or component tree exceeds parser limits");
				return false;
			}
			TArray<FReference>* ChildReferences = &OutReferences;
			if (DesignJsonUtils::GetString(Node, TEXT("type")) == TEXT("INSTANCE"))
			{
				FString Key = DesignJsonUtils::GetString(Node, TEXT("mainComponentKey"));
				if (Key.IsEmpty()) Key = DesignJsonUtils::GetString(Node, TEXT("componentId"));
				if (Key.IsEmpty())
				{
					Error = FString::Printf(TEXT("Instance '%s' has no component identity"),
						*DesignJsonUtils::GetString(Node, TEXT("id")));
					return false;
				}
				FReference& Reference = OutReferences.AddDefaulted_GetRef();
				Reference.Key = MoveTemp(Key);
				ChildReferences = &Reference.Children;
			}
			if (Node->HasField(TEXT("children")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				if (!Node->TryGetArrayField(TEXT("children"), Children))
				{
					Error = TEXT("Component children must be an array");
					return false;
				}
				for (const auto& Child : *Children)
				{
					if (!Child || Child->Type != EJson::Object || !CollectReferences(Child->AsObject(), *ChildReferences, Budget, Depth + 1, Error))
					{
						if (Error.IsEmpty()) Error = TEXT("Component child must be an object");
						return false;
					}
				}
			}
			return true;
		}
	}

	bool FilterRestFileToSelectedRoot(
		const TSharedPtr<FJsonObject>& Root,
		const FString& SelectedRootId,
		FString& OutError)
	{
		OutError.Reset();
		const TSharedPtr<FJsonObject>* DocumentPtr = nullptr;
		const TSharedPtr<FJsonObject>* BundlePtr = nullptr;
		if (!Root || SelectedRootId.IsEmpty()
			|| !Root->TryGetObjectField(TEXT("document"), DocumentPtr)
			|| !Root->TryGetObjectField(TEXT("design2umgComponentDependencies"), BundlePtr))
		{
			OutError = TEXT("Selected Figma root requires a prepared REST document and dependency bundle");
			return false;
		}

		TSharedPtr<FJsonObject> Selected;
		FDesignParserBudget SearchBudget;
		if (!FindNodeById(*DocumentPtr, SelectedRootId, Selected, SearchBudget, 0, OutError)) return false;
		if (!Selected)
		{
			OutError = FString::Printf(TEXT("Selected Figma import root '%s' was not found"), *SelectedRootId);
			return false;
		}

		TSet<FString> SelectedNodeIds;
		FDesignParserBudget CollectionBudget;
		if (!CollectNodeIds(Selected, SelectedNodeIds, CollectionBudget, 0, OutError)) return false;
		// ParseDocument already accepts a FRAME/COMPONENT root. Keeping the DOCUMENT
		// wrapper would reinterpret the selected frame as a page and drop its bounds.
		Root->SetObjectField(TEXT("document"), Selected);

		const TSharedPtr<FJsonObject> Bundle = *BundlePtr;
		const TSharedPtr<FJsonObject>* InstancesPtr = nullptr;
		if (!Bundle->TryGetObjectField(TEXT("instances"), InstancesPtr))
		{
			OutError = TEXT("Prepared Figma dependency bundle has no instances object");
			return false;
		}
		TSharedRef<FJsonObject> FilteredInstances = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*InstancesPtr)->Values)
		{
			if (SelectedNodeIds.Contains(Pair.Key)) FilteredInstances->SetField(Pair.Key, Pair.Value);
		}
		Bundle->SetObjectField(TEXT("instances"), FilteredInstances);

		const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
		if (!Bundle->TryGetArrayField(TEXT("errors"), Errors))
		{
			OutError = TEXT("Prepared Figma dependency bundle has no errors array");
			return false;
		}
		TArray<TSharedPtr<FJsonValue>> FilteredErrors;
		for (const TSharedPtr<FJsonValue>& Error : *Errors)
		{
			if (Error && Error->Type == EJson::Object
				&& SelectedNodeIds.Contains(DesignJsonUtils::GetString(Error->AsObject(), TEXT("nodeId"))))
			{
				FilteredErrors.Add(Error);
			}
		}
		Bundle->SetArrayField(TEXT("errors"), MoveTemp(FilteredErrors));
		return true;
	}

	bool Parse(const FString& Json, FCatalog& OutCatalog, FString& OutError)
	{
		OutCatalog = FCatalog();
		OutError.Reset();
		TSharedPtr<FJsonObject> Root;
		double Version = 0;
		const TArray<TSharedPtr<FJsonValue>>* Definitions = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
		const TSharedPtr<FJsonObject>* Instances = nullptr;
		if (!DesignJsonUtils::TryParseObject(Json, Root)
			|| !Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1
			|| !Root->TryGetArrayField(TEXT("definitions"), Definitions)
			|| !Root->TryGetArrayField(TEXT("errors"), Errors)
			|| !Root->TryGetObjectField(TEXT("instances"), Instances))
		{
			OutError = TEXT("Invalid component dependency bundle: expected schemaVersion 1, definitions/errors arrays and instances object");
			return false;
		}
		FCatalog Catalog;
		FDesignParserBudget Budget;
		for (const auto& Value : *Definitions)
		{
			const auto Entry = Value && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			const TSharedPtr<FJsonObject>* Document = nullptr;
			FDefinition Definition;
			if (!Entry || !Entry->TryGetObjectField(TEXT("document"), Document))
			{
				OutError = TEXT("Component definition is missing its document");
				return false;
			}
			Definition.NodeId = DesignJsonUtils::GetString(Entry, TEXT("id"));
			Definition.Key = DesignJsonUtils::GetString(Entry, TEXT("key"));
			if (Definition.Key.IsEmpty()) Definition.Key = Definition.NodeId;
			Definition.ComponentSetId = DesignJsonUtils::GetString(Entry, TEXT("componentSetId"));
			Definition.ComponentSetKey = DesignJsonUtils::GetString(Entry, TEXT("componentSetKey"));
			Definition.Document = *Document;
			if (Definition.NodeId.IsEmpty() || Catalog.Definitions.Contains(Definition.Key)
				|| DesignJsonUtils::GetString(*Document, TEXT("id")) != Definition.NodeId
				|| DesignJsonUtils::GetString(*Document, TEXT("type")) != TEXT("COMPONENT"))
			{
				OutError = FString::Printf(TEXT("Invalid or duplicate component definition '%s'"), *Definition.Key);
				return false;
			}
			if (!CollectReferences(*Document, Definition.References, Budget, 0, OutError)) return false;
			const FString Key = Definition.Key;
			Catalog.Definitions.Add(Key, MoveTemp(Definition));
		}
		for (const auto& Pair : (*Instances)->Values)
		{
			const auto Entry = Pair.Value && Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
			FString Key = Entry ? DesignJsonUtils::GetString(Entry, TEXT("key")) : FString();
			if (Key.IsEmpty() && Entry) Key = DesignJsonUtils::GetString(Entry, TEXT("id"));
			if (Pair.Key.IsEmpty() || Key.IsEmpty())
			{
				OutError = TEXT("Instance dependency is missing a node or component identity");
				return false;
			}
			Catalog.InstanceKeys.Add(FString(Pair.Key), Key);
		}
		for (const auto& Value : *Errors)
		{
			const auto Entry = Value && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			FSourceError Error;
			if (Entry)
			{
				Error.Kind = DesignJsonUtils::GetString(Entry, TEXT("kind"));
				Error.NodeId = DesignJsonUtils::GetString(Entry, TEXT("nodeId"));
				Error.Message = DesignJsonUtils::GetString(Entry, TEXT("message"));
			}
			if (Error.Kind.IsEmpty() || Error.NodeId.IsEmpty() || Error.Message.IsEmpty())
			{
				OutError = TEXT("Malformed component source error");
				return false;
			}
			Catalog.Errors.Add(MoveTemp(Error));
		}
		OutCatalog = MoveTemp(Catalog);
		return true;
	}

	bool BuildOrder(const FCatalog& Catalog, const TArray<FString>& RequestedKeys,
		const TSet<FString>& ExistingWidgetKeys, TArray<FString>& OutOrder, FString& OutError)
	{
		OutOrder.Reset();
		OutError.Reset();
		TSet<FString> Active;
		TSet<FString> Done;
		TArray<FString> Order;
		int32 TraversalDepth = 0;
		TFunction<bool(const FString&)> Visit;
		TFunction<bool(const TArray<FReference>&)> VisitReferences = [&](const TArray<FReference>& References)
		{
			if (References.IsEmpty()) return true;
			if (TraversalDepth >= DesignTreeLimits::MaxNodeDepth)
			{
				OutError = TEXT("Resolved component reference tree exceeds the supported depth");
				return false;
			}
			TGuardValue<int32> DepthGuard(TraversalDepth, TraversalDepth + 1);
			for (const FReference& Reference : References)
			{
				if (ExistingWidgetKeys.Contains(Reference.Key)) continue;
				if (!Visit(Reference.Key) || !VisitReferences(Reference.Children)) return false;
			}
			return true;
		};
		Visit = [&](const FString& Key)
		{
			if (ExistingWidgetKeys.Contains(Key) || Done.Contains(Key)) return true;
			if (Active.Contains(Key) || TraversalDepth >= DesignTreeLimits::MaxNodeDepth)
			{
				OutError = FString::Printf(TEXT("Component dependency cycle or depth limit at '%s'"), *Key);
				return false;
			}
			TGuardValue<int32> DepthGuard(TraversalDepth, TraversalDepth + 1);
			const FDefinition* Definition = Catalog.Definitions.Find(Key);
			if (!Definition)
			{
				OutError = FString::Printf(TEXT("Component definition '%s' is missing or inaccessible"), *Key);
				for (const FSourceError& SourceError : Catalog.Errors)
				{
					const FString* InstanceKey = Catalog.InstanceKeys.Find(SourceError.NodeId);
					if (SourceError.NodeId == Key || (InstanceKey && *InstanceKey == Key))
						OutError += TEXT(": ") + SourceError.Message;
				}
				return false;
			}
			Active.Add(Key);
			if (!VisitReferences(Definition->References)) return false;
			Active.Remove(Key);
			Done.Add(Key);
			Order.Add(Key);
			return true;
		};
		for (const FString& Key : RequestedKeys)
		{
			if (!Visit(Key)) return false;
		}
		OutOrder = MoveTemp(Order);
		return true;
	}
}
