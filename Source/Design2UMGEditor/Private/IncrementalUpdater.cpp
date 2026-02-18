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

bool FIncrementalUpdater::ShowDiffPreviewDialog(FDiffResult& InOutDiff)
{
	if (!FSlateApplication::IsInitialized() || !InOutDiff.HasChanges())
		return false;

	TSharedPtr<bool> bConfirmed = MakeShared<bool>(false);
	TSharedPtr<FDiffResult> DiffPtr = MakeShared<FDiffResult>(InOutDiff);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DiffTitle", "Incremental Update Preview"))
		.ClientSize(FVector2D(520.0f, 400.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	TWeakPtr<SWindow> WeakWindow = Window;

	TSharedRef<SScrollBox> EntryList = SNew(SScrollBox);
	for (int32 i = 0; i < DiffPtr->Entries.Num(); ++i)
	{
		FDiffEntry& Entry = DiffPtr->Entries[i];
		const TCHAR* ActionStr = Entry.Action == EDiffAction::Added ? TEXT("+") :
			Entry.Action == EDiffAction::Modified ? TEXT("~") : TEXT("-");
		FLinearColor Color = Entry.Action == EDiffAction::Added ? FLinearColor(0.3f, 0.9f, 0.3f) :
			Entry.Action == EDiffAction::Modified ? FLinearColor(1.0f, 0.7f, 0.0f) :
			FLinearColor(0.9f, 0.3f, 0.3f);

		EntryList->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 2.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([DiffPtr, i]() { return DiffPtr->Entries[i].bAccepted ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([DiffPtr, i](ECheckBoxState State) { DiffPtr->Entries[i].bAccepted = (State == ECheckBoxState::Checked); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 2.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("[%s] %s (%s)"), ActionStr, *Entry.NodeName, *Entry.NodeType)))
				.ColorAndOpacity(FSlateColor(Color))
			]
		];
	}

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 12.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("DiffSummary", "Changes: {0} added, {1} modified, {2} removed"),
				FText::AsNumber(InOutDiff.CountAdded()),
				FText::AsNumber(InOutDiff.CountModified()),
				FText::AsNumber(InOutDiff.CountRemoved())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(12.0f, 4.0f)
		[
			EntryList
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DiffCancel", "Cancel"))
				.OnClicked_Lambda([WeakWindow]()
				{
					if (auto Win = WeakWindow.Pin()) Win->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DiffApply", "Apply Selected"))
				.OnClicked_Lambda([WeakWindow, bConfirmed]()
				{
					*bConfirmed = true;
					if (auto Win = WeakWindow.Pin()) Win->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	if (*bConfirmed)
		InOutDiff = *DiffPtr;
	return *bConfirmed;
}

FUMGWidgetNode FIncrementalUpdater::FilterByAccepted(const FUMGWidgetNode& Root, const FDiffResult& Diff)
{
	// Build set of rejected node IDs
	TSet<FString> RejectedIds;
	for (const auto& E : Diff.Entries)
	{
		if (!E.bAccepted)
			RejectedIds.Add(E.NodeId);
	}

	if (RejectedIds.Num() == 0)
		return Root;

	// Recursive filter: remove rejected nodes from tree
	TFunction<FUMGWidgetNode(const FUMGWidgetNode&)> Filter = [&](const FUMGWidgetNode& Node) -> FUMGWidgetNode
	{
		FUMGWidgetNode Result = Node;
		Result.Children.Empty();
		for (const auto& Child : Node.Children)
		{
			if (!RejectedIds.Contains(Child.Id))
				Result.Children.Add(Filter(Child));
		}
		return Result;
	};

	return Filter(Root);
}

#undef LOCTEXT_NAMESPACE
