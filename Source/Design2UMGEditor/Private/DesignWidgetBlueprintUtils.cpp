// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignWidgetBlueprintUtils.h"

#include "Misc/EngineVersionComparison.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

#if UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelWidget.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprintEditor.h"
#endif

namespace DesignWidgetBlueprintUtils
{
	void DeleteSubtreeSilently(UWidgetBlueprint* Blueprint, UWidget* Root)
	{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		FWidgetBlueprintEditorUtils::DeleteWidgets(
			Blueprint, { Root }, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
#else
		// The editor-only DeleteWidgets overload requires a live editor in these engines.
		// Use its underlying asset operations; no editor is created for an import.
		FScopedTransaction Transaction(NSLOCTEXT("Design2UMG", "RemoveWidgetSubtree", "Remove Widget"));
		UWidgetTree* Tree = Blueprint->WidgetTree;
		Tree->SetFlags(RF_Transactional);
		Tree->Modify();
		Blueprint->Modify();

		TArray<UWidget*> RemovedWidgets{ Root };
		UWidgetTree::GetChildWidgets(Root, RemovedWidgets);
		for (UWidget* Widget : RemovedWidgets)
		{
			Widget->SetFlags(RF_Transactional);
			Widget->Modify();
		}
		if (UPanelWidget* Parent = Root->GetParent())
		{
			Parent->SetFlags(RF_Transactional);
			Parent->Modify();
		}

		bool bRemoved = Tree->RemoveWidget(Root);
		if (Root->GetParent() == nullptr)
		{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
			bool bNamedSlotContentRemoved = false;
			Tree->ForEachWidget([&](UWidget* HostWidget)
			{
				if (bNamedSlotContentRemoved)
				{
					return;
				}

				if (INamedSlotInterface* Host = Cast<INamedSlotInterface>(HostWidget))
				{
					FName SlotName = NAME_None;
					TArray<FName> SlotNames;
					Host->GetSlotNames(SlotNames);
					for (const FName CandidateSlotName : SlotNames)
					{
						if (Host->GetContentForSlot(CandidateSlotName) == Root)
						{
							SlotName = CandidateSlotName;
							break;
						}
					}
					if (!SlotName.IsNone())
					{
						HostWidget->SetFlags(RF_Transactional);
						HostWidget->Modify();
						Host->SetContentForSlot(SlotName, nullptr);
						bNamedSlotContentRemoved = true;
					}
				}
			});
			bRemoved |= bNamedSlotContentRemoved;
#else
			UWidget* HostWidget = FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(Root, Tree);
			if (TScriptInterface<INamedSlotInterface> Host = TScriptInterface<INamedSlotInterface>(HostWidget))
			{
				HostWidget->SetFlags(RF_Transactional);
				HostWidget->Modify();
				bRemoved |= FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(Root, Host);
			}
#endif
		}

#if !UE_VERSION_OLDER_THAN(5, 3, 0)
		UUserWidget* CDO = Blueprint->GeneratedClass
			? Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>() : nullptr;
#endif
		for (UWidget* Widget : RemovedWidgets)
		{
			const FName Name = Widget->GetFName();
			Blueprint->Bindings.RemoveAll([Widget](const FDelegateEditorBinding& Binding)
			{
				return Binding.ObjectName == Widget->GetName();
			});
			if (FBlueprintEditorUtils::IsVariableUsed(Blueprint, Name))
			{
				FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Name);
			}
#if !UE_VERSION_OLDER_THAN(5, 3, 0)
			if (CDO && CDO->GetDesiredFocusWidgetName() == Name)
			{
				CDO->SetFlags(RF_Transactional);
				CDO->Modify();
				CDO->SetDesiredFocusWidget(NAME_None);
				constexpr bool bFocusIfOpen = false;
				FWidgetBlueprintEditor* Editor = static_cast<FWidgetBlueprintEditor*>(
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, bFocusIfOpen));
				if (Editor)
				{
					if (UUserWidget* Preview = Editor->GetPreview())
					{
						Preview->SetFlags(RF_Transactional);
						Preview->Modify();
						Preview->SetDesiredFocusWidget(NAME_None);
					}
				}
			}
#endif
			Widget->Rename(nullptr, GetTransientPackage());
		}
		if (bRemoved)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
#endif
	}
}
