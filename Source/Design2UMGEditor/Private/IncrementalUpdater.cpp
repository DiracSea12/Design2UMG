// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "IncrementalUpdater.h"
#include "UMGWidgetUtils.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "IncrementalUpdater"

int32 FDiffResult::CountAdded() const
{
	int32 N = 0;
	for (const auto& E : Entries)
		if (E.Action == EDiffAction::Added) ++N;
	return N;
}

int32 FDiffResult::CountModified() const
{
	int32 N = 0;
	for (const auto& E : Entries)
		if (E.Action == EDiffAction::Modified) ++N;
	return N;
}

int32 FDiffResult::CountRemoved() const
{
	int32 N = 0;
	for (const auto& E : Entries)
		if (E.Action == EDiffAction::Removed) ++N;
	return N;
}

namespace
{

void CollectExistingIds(UWidgetBlueprint* BP, TMap<FString, UWidget*>& OutMap)
{
	if (!BP || !BP->WidgetTree) return;

	BP->WidgetTree->ForEachWidget([&OutMap](UWidget* W)
	{
		FString NodeId = FUMGWidgetUtils::FindNodeId(W);
		if (!NodeId.IsEmpty())
			OutMap.Add(NodeId, W);
	});
}

void CollectNewIds(const FUMGWidgetNode& Node, TMap<FString, const FUMGWidgetNode*>& OutMap)
{
	if (!Node.Id.IsEmpty())
		OutMap.Add(Node.Id, &Node);
	for (const auto& Child : Node.Children)
		CollectNewIds(Child, OutMap);
}

bool IsNodeModified(const FUMGWidgetNode& Node, UWidget* Widget)
{
	// Type change = modified
	if (Widget->GetClass() != FUMGWidgetUtils::ResolveWidgetClass(Node.Type))
		return true;

	// Name change
	FName Expected = FUMGWidgetUtils::SanitizeWidgetName(Node.Name, Node.Type);
	if (Widget->GetFName() != Expected)
		return true;

	return false;
}

} // namespace

FDiffResult FIncrementalUpdater::ComputeDiff(
	const FUMGWidgetNode& NewRoot,
	UWidgetBlueprint* ExistingBP)
{
	FDiffResult Result;

	TMap<FString, UWidget*> ExistingMap;
	CollectExistingIds(ExistingBP, ExistingMap);

	TMap<FString, const FUMGWidgetNode*> NewMap;
	CollectNewIds(NewRoot, NewMap);

	// Added + Modified
	for (const auto& Pair : NewMap)
	{
		if (UWidget** Found = ExistingMap.Find(Pair.Key))
		{
			if (IsNodeModified(*Pair.Value, *Found))
			{
				Result.Entries.Add({EDiffAction::Modified, Pair.Key, Pair.Value->Name, Pair.Value->Type});
			}
		}
		else
		{
			Result.Entries.Add({EDiffAction::Added, Pair.Key, Pair.Value->Name, Pair.Value->Type});
		}
	}

	// Removed
	for (const auto& Pair : ExistingMap)
	{
		if (!NewMap.Contains(Pair.Key))
		{
			Result.Entries.Add({EDiffAction::Removed, Pair.Key, Pair.Value->GetName(), TEXT("")});
		}
	}

	return Result;
}
