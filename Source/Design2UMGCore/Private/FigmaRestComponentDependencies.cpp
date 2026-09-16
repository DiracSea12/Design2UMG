// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "FigmaComponentDependencies.h"
#include "DesignJsonUtils.h"
#include "DesignTreeLimits.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"

bool FigmaComponentDependencies::PrepareRestFile(const FString& Json, const FString& FileKey,
	FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Document = nullptr;
	if (!DesignJsonUtils::TryParseObject(Json, Root) || !Root->TryGetObjectField(TEXT("document"), Document) || FileKey.IsEmpty())
	{
		OutError = TEXT("Figma REST response requires a document and file identity");
		return false;
	}
	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	Root->TryGetObjectField(TEXT("components"), Metadata);
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	Root->TryGetObjectField(TEXT("componentSets"), Sets);
	auto ComponentKey = [&](const FString& Id)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (Metadata && (*Metadata)->TryGetObjectField(Id, Entry))
		{
			const FString Key = DesignJsonUtils::GetString(*Entry, TEXT("key"));
			if (!Key.IsEmpty()) return Key;
		}
		return Id;
	};
	TSharedRef<FJsonObject> Instances = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Definitions;
	FDesignParserBudget Budget;
	TFunction<bool(const TSharedPtr<FJsonObject>&, const FString&, int32, bool)> Visit;
	Visit = [&](const TSharedPtr<FJsonObject>& Node, const FString& ParentSet, int32 Depth, bool InDefinition)
	{
		if (!Node || !Budget.TryEnterNode(Depth))
		{
			OutError = TEXT("Figma REST component tree is invalid or exceeds parser limits");
			return false;
		}
		const FString Type = DesignJsonUtils::GetString(Node, TEXT("type"));
		const FString Id = DesignJsonUtils::GetString(Node, TEXT("id"));
		if (Type == TEXT("INSTANCE"))
		{
			const FString MainId = DesignJsonUtils::GetString(Node, TEXT("componentId"));
			const FString Key = ComponentKey(MainId);
			Node->SetStringField(TEXT("mainComponentKey"), Key);
			TSharedRef<FJsonObject> Identity = MakeShared<FJsonObject>();
			Identity->SetStringField(TEXT("id"), MainId);
			Identity->SetStringField(TEXT("key"), Key);
			if (!InDefinition) Instances->SetObjectField(Id, Identity);
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (Metadata && (*Metadata)->TryGetObjectField(MainId, Entry))
			{
				const FString SetId = DesignJsonUtils::GetString(*Entry, TEXT("componentSetId"));
				if (!SetId.IsEmpty()) Node->SetStringField(TEXT("componentSetId"), SetId);
				const TSharedPtr<FJsonObject>* Set = nullptr;
				if (Sets && (*Sets)->TryGetObjectField(SetId, Set))
					Node->SetStringField(TEXT("componentSetKey"), DesignJsonUtils::GetString(*Set, TEXT("key")));
			}
		}
		if (Type == TEXT("COMPONENT"))
		{
			TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
			Definition->SetStringField(TEXT("id"), Id);
			Definition->SetStringField(TEXT("key"), ComponentKey(Id));
			Definition->SetStringField(TEXT("componentSetId"), ParentSet);
			const TSharedPtr<FJsonObject>* Set = nullptr;
			if (Sets && (*Sets)->TryGetObjectField(ParentSet, Set))
				Definition->SetStringField(TEXT("componentSetKey"), DesignJsonUtils::GetString(*Set, TEXT("key")));
			Definition->SetObjectField(TEXT("document"), Node);
			Definitions.Add(MakeShared<FJsonValueObject>(Definition));
		}
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Node->TryGetArrayField(TEXT("children"), Children))
		{
			for (const auto& Child : *Children)
			{
				if (!Child || Child->Type != EJson::Object || !Visit(Child->AsObject(), Type == TEXT("COMPONENT_SET") ? Id : FString(), Depth + 1, InDefinition || Type == TEXT("COMPONENT"))) return false;
			}
		}
		return true;
	};
	if (!Visit(*Document, FString(), 0, false)) return false;
	TSharedRef<FJsonObject> Bundle = MakeShared<FJsonObject>();
	Bundle->SetNumberField(TEXT("schemaVersion"), 1);
	Bundle->SetArrayField(TEXT("definitions"), Definitions);
	Bundle->SetObjectField(TEXT("instances"), Instances);
	Bundle->SetArrayField(TEXT("errors"), {});
	Root->SetObjectField(TEXT("design2umgComponentDependencies"), Bundle);
	Root->SetStringField(TEXT("fileKey"), FileKey);
	return FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJson));
}
